/*
 *    DOM Document implementation
 *
 * Copyright 2005 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include "config.h"

#include <stdarg.h>
#include <assert.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "ole2.h"
#include "msxml2.h"
#include "wininet.h"
#include "urlmon.h"
#include "winreg.h"
#include "shlwapi.h"

#include "wine/debug.h"

#include "msxml_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

#ifdef HAVE_LIBXML2

typedef struct {
    const struct IBindStatusCallbackVtbl *lpVtbl;
} bsc;

static HRESULT WINAPI bsc_QueryInterface(
    IBindStatusCallback *iface,
    REFIID riid,
    LPVOID *ppobj )
{
    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IBindStatusCallback))
    {
        IBindStatusCallback_AddRef( iface );
        *ppobj = iface;
        return S_OK;
    }

    FIXME("interface %s not implemented\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI bsc_AddRef(
    IBindStatusCallback *iface )
{
    return 2;
}

static ULONG WINAPI bsc_Release(
    IBindStatusCallback *iface )
{
    return 1;
}

static HRESULT WINAPI bsc_OnStartBinding(
        IBindStatusCallback* iface,
        DWORD dwReserved,
        IBinding* pib)
{
    return S_OK;
}

static HRESULT WINAPI bsc_GetPriority(
        IBindStatusCallback* iface,
        LONG* pnPriority)
{
    return S_OK;
}

static HRESULT WINAPI bsc_OnLowResource(
        IBindStatusCallback* iface,
        DWORD reserved)
{
    return S_OK;
}

static HRESULT WINAPI bsc_OnProgress(
        IBindStatusCallback* iface,
        ULONG ulProgress,
        ULONG ulProgressMax,
        ULONG ulStatusCode,
        LPCWSTR szStatusText)
{
    return S_OK;
}

static HRESULT WINAPI bsc_OnStopBinding(
        IBindStatusCallback* iface,
        HRESULT hresult,
        LPCWSTR szError)
{
    return S_OK;
}

static HRESULT WINAPI bsc_GetBindInfo(
        IBindStatusCallback* iface,
        DWORD* grfBINDF,
        BINDINFO* pbindinfo)
{
    *grfBINDF = BINDF_RESYNCHRONIZE;
    
    return S_OK;
}

static HRESULT WINAPI bsc_OnDataAvailable(
        IBindStatusCallback* iface,
        DWORD grfBSCF,
        DWORD dwSize,
        FORMATETC* pformatetc,
        STGMEDIUM* pstgmed)
{
    return S_OK;
}

static HRESULT WINAPI bsc_OnObjectAvailable(
        IBindStatusCallback* iface,
        REFIID riid,
        IUnknown* punk)
{
    return S_OK;
}

static const struct IBindStatusCallbackVtbl bsc_vtbl =
{
    bsc_QueryInterface,
    bsc_AddRef,
    bsc_Release,
    bsc_OnStartBinding,
    bsc_GetPriority,
    bsc_OnLowResource,
    bsc_OnProgress,
    bsc_OnStopBinding,
    bsc_GetBindInfo,
    bsc_OnDataAvailable,
    bsc_OnObjectAvailable
};

static bsc domdoc_bsc = { &bsc_vtbl };

typedef struct _domdoc
{
    const struct IXMLDOMDocument2Vtbl *lpVtbl;
    LONG ref;
    VARIANT_BOOL async;
    VARIANT_BOOL validating;
    VARIANT_BOOL resolving;
    VARIANT_BOOL preserving;
    IUnknown *node_unk;
    IXMLDOMNode *node;
    IXMLDOMSchemaCollection *schema;
    HRESULT error;
} domdoc;

LONG xmldoc_add_ref(xmlDocPtr doc)
{
    LONG ref = InterlockedIncrement((LONG*)&doc->_private);
    TRACE("%d\n", ref);
    return ref;
}

LONG xmldoc_release(xmlDocPtr doc)
{
    LONG ref = InterlockedDecrement((LONG*)&doc->_private);
    TRACE("%d\n", ref);
    if(ref == 0)
    {
        TRACE("freeing docptr %p\n", doc);
        xmlFreeDoc(doc);
    }

    return ref;
}

static inline domdoc *impl_from_IXMLDOMDocument2( IXMLDOMDocument2 *iface )
{
    return (domdoc *)((char*)iface - FIELD_OFFSET(domdoc, lpVtbl));
}

static inline xmlDocPtr get_doc( domdoc *This )
{
    return (xmlDocPtr) xmlNodePtr_from_domnode( This->node, XML_DOCUMENT_NODE );
}

static HRESULT WINAPI domdoc_QueryInterface( IXMLDOMDocument2 *iface, REFIID riid, void** ppvObject )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%p %s %p\n", This, debugstr_guid( riid ), ppvObject );

    if ( IsEqualGUID( riid, &IID_IUnknown ) ||
         IsEqualGUID( riid, &IID_IXMLDOMDocument ) ||
         IsEqualGUID( riid, &IID_IXMLDOMDocument2 ) )
    {
        *ppvObject = iface;
    }
    else if ( IsEqualGUID( riid, &IID_IXMLDOMNode ) ||
              IsEqualGUID( riid, &IID_IDispatch ) )
    {
        return IUnknown_QueryInterface(This->node_unk, riid, ppvObject);
    }
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IXMLDOMDocument_AddRef( iface );

    return S_OK;
}


static ULONG WINAPI domdoc_AddRef(
     IXMLDOMDocument2 *iface )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    TRACE("%p\n", This );
    return InterlockedIncrement( &This->ref );
}


static ULONG WINAPI domdoc_Release(
     IXMLDOMDocument2 *iface )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    LONG ref;

    TRACE("%p\n", This );

    ref = InterlockedDecrement( &This->ref );
    if ( ref == 0 )
    {
        IUnknown_Release( This->node_unk );
        if(This->schema) IXMLDOMSchemaCollection_Release( This->schema );
        HeapFree( GetProcessHeap(), 0, This );
    }

    return ref;
}

static HRESULT WINAPI domdoc_GetTypeInfoCount( IXMLDOMDocument2 *iface, UINT* pctinfo )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_GetTypeInfo(
    IXMLDOMDocument2 *iface,
    UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_GetIDsOfNames(
    IXMLDOMDocument2 *iface,
    REFIID riid,
    LPOLESTR* rgszNames,
    UINT cNames,
    LCID lcid,
    DISPID* rgDispId)
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_Invoke(
    IXMLDOMDocument2 *iface,
    DISPID dispIdMember,
    REFIID riid,
    LCID lcid,
    WORD wFlags,
    DISPPARAMS* pDispParams,
    VARIANT* pVarResult,
    EXCEPINFO* pExcepInfo,
    UINT* puArgErr)
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_get_nodeName(
    IXMLDOMDocument2 *iface,
    BSTR* name )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nodeName( This->node, name );
}


static HRESULT WINAPI domdoc_get_nodeValue(
    IXMLDOMDocument2 *iface,
    VARIANT* value )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nodeValue( This->node, value );
}


static HRESULT WINAPI domdoc_put_nodeValue(
    IXMLDOMDocument2 *iface,
    VARIANT value)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_put_nodeValue( This->node, value );
}


static HRESULT WINAPI domdoc_get_nodeType(
    IXMLDOMDocument2 *iface,
    DOMNodeType* type )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nodeType( This->node, type );
}


static HRESULT WINAPI domdoc_get_parentNode(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** parent )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_parentNode( This->node, parent );
}


static HRESULT WINAPI domdoc_get_childNodes(
    IXMLDOMDocument2 *iface,
    IXMLDOMNodeList** childList )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_childNodes( This->node, childList );
}


static HRESULT WINAPI domdoc_get_firstChild(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** firstChild )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_firstChild( This->node, firstChild );
}


static HRESULT WINAPI domdoc_get_lastChild(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** lastChild )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_lastChild( This->node, lastChild );
}


static HRESULT WINAPI domdoc_get_previousSibling(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** previousSibling )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_previousSibling( This->node, previousSibling );
}


static HRESULT WINAPI domdoc_get_nextSibling(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** nextSibling )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nextSibling( This->node, nextSibling );
}


static HRESULT WINAPI domdoc_get_attributes(
    IXMLDOMDocument2 *iface,
    IXMLDOMNamedNodeMap** attributeMap )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_attributes( This->node, attributeMap );
}


static HRESULT WINAPI domdoc_insertBefore(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* newChild,
    VARIANT refChild,
    IXMLDOMNode** outNewChild )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_insertBefore( This->node, newChild, refChild, outNewChild );
}


static HRESULT WINAPI domdoc_replaceChild(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* newChild,
    IXMLDOMNode* oldChild,
    IXMLDOMNode** outOldChild)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_replaceChild( This->node, newChild, oldChild, outOldChild );
}


static HRESULT WINAPI domdoc_removeChild(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* childNode,
    IXMLDOMNode** oldChild)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_removeChild( This->node, childNode, oldChild );
}


static HRESULT WINAPI domdoc_appendChild(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* newChild,
    IXMLDOMNode** outNewChild)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_appendChild( This->node, newChild, outNewChild );
}


static HRESULT WINAPI domdoc_hasChildNodes(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* hasChild)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_hasChildNodes( This->node, hasChild );
}


static HRESULT WINAPI domdoc_get_ownerDocument(
    IXMLDOMDocument2 *iface,
    IXMLDOMDocument** DOMDocument)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_ownerDocument( This->node, DOMDocument );
}


static HRESULT WINAPI domdoc_cloneNode(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL deep,
    IXMLDOMNode** cloneRoot)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_cloneNode( This->node, deep, cloneRoot );
}


static HRESULT WINAPI domdoc_get_nodeTypeString(
    IXMLDOMDocument2 *iface,
    BSTR* nodeType )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nodeTypeString( This->node, nodeType );
}


static HRESULT WINAPI domdoc_get_text(
    IXMLDOMDocument2 *iface,
    BSTR* text )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_text( This->node, text );
}


static HRESULT WINAPI domdoc_put_text(
    IXMLDOMDocument2 *iface,
    BSTR text )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_put_text( This->node, text );
}


static HRESULT WINAPI domdoc_get_specified(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isSpecified )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_specified( This->node, isSpecified );
}


static HRESULT WINAPI domdoc_get_definition(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode** definitionNode )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_definition( This->node, definitionNode );
}


static HRESULT WINAPI domdoc_get_nodeTypedValue(
    IXMLDOMDocument2 *iface,
    VARIANT* typedValue )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_nodeTypedValue( This->node, typedValue );
}

static HRESULT WINAPI domdoc_put_nodeTypedValue(
    IXMLDOMDocument2 *iface,
    VARIANT typedValue )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_put_nodeTypedValue( This->node, typedValue );
}


static HRESULT WINAPI domdoc_get_dataType(
    IXMLDOMDocument2 *iface,
    VARIANT* dataTypeName )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_dataType( This->node, dataTypeName );
}


static HRESULT WINAPI domdoc_put_dataType(
    IXMLDOMDocument2 *iface,
    BSTR dataTypeName )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_put_dataType( This->node, dataTypeName );
}


static HRESULT WINAPI domdoc_get_xml(
    IXMLDOMDocument2 *iface,
    BSTR* xmlString )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_xml( This->node, xmlString );
}


static HRESULT WINAPI domdoc_transformNode(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* styleSheet,
    BSTR* xmlString )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_transformNode( This->node, styleSheet, xmlString );
}


static HRESULT WINAPI domdoc_selectNodes(
    IXMLDOMDocument2 *iface,
    BSTR queryString,
    IXMLDOMNodeList** resultList )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_selectNodes( This->node, queryString, resultList );
}


static HRESULT WINAPI domdoc_selectSingleNode(
    IXMLDOMDocument2 *iface,
    BSTR queryString,
    IXMLDOMNode** resultNode )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_selectSingleNode( This->node, queryString, resultNode );
}


static HRESULT WINAPI domdoc_get_parsed(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isParsed )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_parsed( This->node, isParsed );
}


static HRESULT WINAPI domdoc_get_namespaceURI(
    IXMLDOMDocument2 *iface,
    BSTR* namespaceURI )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_namespaceURI( This->node, namespaceURI );
}


static HRESULT WINAPI domdoc_get_prefix(
    IXMLDOMDocument2 *iface,
    BSTR* prefixString )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_prefix( This->node, prefixString );
}


static HRESULT WINAPI domdoc_get_baseName(
    IXMLDOMDocument2 *iface,
    BSTR* nameString )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_get_baseName( This->node, nameString );
}


static HRESULT WINAPI domdoc_transformNodeToObject(
    IXMLDOMDocument2 *iface,
    IXMLDOMNode* stylesheet,
    VARIANT outputObject)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    return IXMLDOMNode_transformNodeToObject( This->node, stylesheet, outputObject );
}


static HRESULT WINAPI domdoc_get_doctype(
    IXMLDOMDocument2 *iface,
    IXMLDOMDocumentType** documentType )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_get_implementation(
    IXMLDOMDocument2 *iface,
    IXMLDOMImplementation** impl )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_get_documentElement(
    IXMLDOMDocument2 *iface,
    IXMLDOMElement** DOMElement )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    xmlDocPtr xmldoc = NULL;
    xmlNodePtr root = NULL;
    IXMLDOMNode *element_node;
    HRESULT hr;

    TRACE("%p\n", This);

    *DOMElement = NULL;

    xmldoc = get_doc( This );

    root = xmlDocGetRootElement( xmldoc );
    if ( !root )
        return S_FALSE;

    element_node = create_node( root );
    if(!element_node) return S_FALSE;

    hr = IXMLDOMNode_QueryInterface(element_node, &IID_IXMLDOMElement, (LPVOID*)DOMElement);
    IXMLDOMNode_Release(element_node);

    return hr;
}


static HRESULT WINAPI domdoc_documentElement(
    IXMLDOMDocument2 *iface,
    IXMLDOMElement* DOMElement )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createElement(
    IXMLDOMDocument2 *iface,
    BSTR tagname,
    IXMLDOMElement** element )
{
    xmlNodePtr xmlnode;
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    xmlChar *xml_name;
    IUnknown *elem_unk;
    HRESULT hr;

    TRACE("%p->(%s,%p)\n", iface, debugstr_w(tagname), element);

    xml_name = xmlChar_from_wchar((WCHAR*)tagname);
    xmlnode = xmlNewDocNode(get_doc(This), NULL, xml_name, NULL);

    TRACE("created xmlptr %p\n", xmlnode);
    elem_unk = create_element(xmlnode, NULL);
    HeapFree(GetProcessHeap(), 0, xml_name);

    hr = IUnknown_QueryInterface(elem_unk, &IID_IXMLDOMElement, (void **)element);
    IUnknown_Release(elem_unk);
    TRACE("returning %p\n", *element);
    return hr;
}


static HRESULT WINAPI domdoc_createDocumentFragment(
    IXMLDOMDocument2 *iface,
    IXMLDOMDocumentFragment** docFrag )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createTextNode(
    IXMLDOMDocument2 *iface,
    BSTR data,
    IXMLDOMText** text )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createComment(
    IXMLDOMDocument2 *iface,
    BSTR data,
    IXMLDOMComment** comment )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createCDATASection(
    IXMLDOMDocument2 *iface,
    BSTR data,
    IXMLDOMCDATASection** cdata )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createProcessingInstruction(
    IXMLDOMDocument2 *iface,
    BSTR target,
    BSTR data,
    IXMLDOMProcessingInstruction** pi )
{
#ifdef HAVE_XMLNEWDOCPI
    xmlNodePtr xmlnode;
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    xmlChar *xml_target, *xml_content;

    TRACE("%p->(%s %s %p)\n", iface, debugstr_w(target), debugstr_w(data), pi);

    xml_target = xmlChar_from_wchar((WCHAR*)target);
    xml_content = xmlChar_from_wchar((WCHAR*)data);

    xmlnode = xmlNewDocPI(get_doc(This), xml_target, xml_content);
    TRACE("created xmlptr %p\n", xmlnode);
    *pi = (IXMLDOMProcessingInstruction*)create_pi(xmlnode);

    HeapFree(GetProcessHeap(), 0, xml_content);
    HeapFree(GetProcessHeap(), 0, xml_target);

    return S_OK;
#else
    FIXME("Libxml 2.6.15 or greater required.\n");
    return E_NOTIMPL;
#endif
}


static HRESULT WINAPI domdoc_createAttribute(
    IXMLDOMDocument2 *iface,
    BSTR name,
    IXMLDOMAttribute** attribute )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_createEntityReference(
    IXMLDOMDocument2 *iface,
    BSTR name,
    IXMLDOMEntityReference** entityRef )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_getElementsByTagName(
    IXMLDOMDocument2 *iface,
    BSTR tagName,
    IXMLDOMNodeList** resultList )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    xmlChar *name;
    TRACE("(%p)->(%s, %p)\n", This, debugstr_w(tagName), resultList);

    name = xmlChar_from_wchar((WCHAR*)tagName);
    *resultList = create_filtered_nodelist((xmlNodePtr)get_doc(This), name, TRUE);
    HeapFree(GetProcessHeap(), 0, name);

    if(!*resultList) return S_FALSE;
    return S_OK;
}

static DOMNodeType get_node_type(VARIANT Type)
{
    if(V_VT(&Type) == VT_I4)
        return V_I4(&Type);

    FIXME("Unsupported variant type %x\n", V_VT(&Type));
    return 0;
}

static HRESULT WINAPI domdoc_createNode(
    IXMLDOMDocument2 *iface,
    VARIANT Type,
    BSTR name,
    BSTR namespaceURI,
    IXMLDOMNode** node )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    DOMNodeType node_type;
    xmlNodePtr xmlnode = NULL;
    xmlChar *xml_name;

    TRACE("(%p)->(type,%s,%s,%p)\n", This, debugstr_w(name), debugstr_w(namespaceURI), node);

    node_type = get_node_type(Type);
    TRACE("node_type %d\n", node_type);

    xml_name = xmlChar_from_wchar((WCHAR*)name);

    switch(node_type)
    {
    case NODE_ELEMENT:
        xmlnode = xmlNewDocNode(get_doc(This), NULL, xml_name, NULL);
        *node = create_node(xmlnode);
        TRACE("created %p\n", xmlnode);
        break;

    default:
        FIXME("unhandled node type %d\n", node_type);
        break;
    }

    HeapFree(GetProcessHeap(), 0, xml_name);

    if(xmlnode && *node)
        return S_OK;

    return E_FAIL;
}

static HRESULT WINAPI domdoc_nodeFromID(
    IXMLDOMDocument2 *iface,
    BSTR idString,
    IXMLDOMNode** node )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static xmlDocPtr doparse( char *ptr, int len )
{
#ifdef HAVE_XMLREADMEMORY
    /*
     * use xmlReadMemory if possible so we can suppress
     * writing errors to stderr
     */
    return xmlReadMemory( ptr, len, NULL, NULL,
                          XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NOBLANKS );
#else
    return xmlParseMemory( ptr, len );
#endif
}

static xmlDocPtr doread( LPWSTR filename )
{
    xmlDocPtr xmldoc = NULL;
    HRESULT hr;
    IBindCtx *pbc;
    IStream *stream, *memstream;
    WCHAR url[INTERNET_MAX_URL_LENGTH];
    BYTE buf[4096];
    DWORD read, written;

    TRACE("%s\n", debugstr_w( filename ));

    if(!PathIsURLW(filename))
    {
        WCHAR fullpath[MAX_PATH];
        DWORD needed = sizeof(url)/sizeof(WCHAR);

        if(!PathSearchAndQualifyW(filename, fullpath, sizeof(fullpath)/sizeof(WCHAR)))
        {
            WARN("can't find path\n");
            return NULL;
        }

        if(FAILED(UrlCreateFromPathW(fullpath, url, &needed, 0)))
        {
            ERR("can't create url from path\n");
            return NULL;
        }
        filename = url;
    }

    hr = CreateBindCtx(0, &pbc);
    if(SUCCEEDED(hr))
    {
        hr = RegisterBindStatusCallback(pbc, (IBindStatusCallback*)&domdoc_bsc.lpVtbl, NULL, 0);
        if(SUCCEEDED(hr))
        {
            IMoniker *moniker;
            hr = CreateURLMoniker(NULL, filename, &moniker);
            if(SUCCEEDED(hr))
            {
                hr = IMoniker_BindToStorage(moniker, pbc, NULL, &IID_IStream, (LPVOID*)&stream);
                IMoniker_Release(moniker);
            }
        }
        IBindCtx_Release(pbc);
    }
    if(FAILED(hr))
        return NULL;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &memstream);
    if(FAILED(hr))
    {
        IStream_Release(stream);
        return NULL;
    }

    do
    {
        IStream_Read(stream, buf, sizeof(buf), &read);
        hr = IStream_Write(memstream, buf, read, &written);
    } while(SUCCEEDED(hr) && written != 0 && read != 0);

    if(SUCCEEDED(hr))
    {
        HGLOBAL hglobal;
        hr = GetHGlobalFromStream(memstream, &hglobal);
        if(SUCCEEDED(hr))
        {
            DWORD len = GlobalSize(hglobal);
            char *ptr = GlobalLock(hglobal);
            if(len != 0)
                xmldoc = doparse( ptr, len );
            GlobalUnlock(hglobal);
        }
    }
    IStream_Release(memstream);
    IStream_Release(stream);
    return xmldoc;
}

static HRESULT WINAPI domdoc_load(
    IXMLDOMDocument2 *iface,
    VARIANT xmlSource,
    VARIANT_BOOL* isSuccessful )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    LPWSTR filename = NULL;
    xmlDocPtr xmldoc = NULL;
    HRESULT hr = S_FALSE;

    TRACE("type %d\n", V_VT(&xmlSource) );

    *isSuccessful = VARIANT_FALSE;

    assert( This->node );

    attach_xmlnode(This->node, NULL);

    switch( V_VT(&xmlSource) )
    {
    case VT_BSTR:
        filename = V_BSTR(&xmlSource);
    }

    if ( filename )
    {
        xmldoc = doread( filename );
    
        if ( !xmldoc )
            This->error = E_FAIL;
        else
        {
            hr = This->error = S_OK;
            *isSuccessful = VARIANT_TRUE;
        }
    }

    if(!xmldoc)
        xmldoc = xmlNewDoc(NULL);

    xmldoc->_private = 0;
    attach_xmlnode(This->node, (xmlNodePtr) xmldoc);

    return hr;
}


static HRESULT WINAPI domdoc_get_readyState(
    IXMLDOMDocument2 *iface,
    long* value )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_get_parseError(
    IXMLDOMDocument2 *iface,
    IXMLDOMParseError** errorObj )
{
    BSTR error_string = NULL;
    static const WCHAR err[] = {'e','r','r','o','r',0};
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    FIXME("(%p)->(%p): creating a dummy parseError\n", iface, errorObj);

    if(This->error)
        error_string = SysAllocString(err);

    *errorObj = create_parseError(This->error, NULL, error_string, NULL, 0, 0, 0);
    if(!*errorObj) return E_OUTOFMEMORY;
    return S_OK;
}


static HRESULT WINAPI domdoc_get_url(
    IXMLDOMDocument2 *iface,
    BSTR* urlString )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_get_async(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isAsync )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%p <- %d\n", isAsync, This->async);
    *isAsync = This->async;
    return S_OK;
}


static HRESULT WINAPI domdoc_put_async(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL isAsync )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%d\n", isAsync);
    This->async = isAsync;
    return S_OK;
}


static HRESULT WINAPI domdoc_abort(
    IXMLDOMDocument2 *iface )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static BOOL bstr_to_utf8( BSTR bstr, char **pstr, int *plen )
{
    UINT len, blen = SysStringLen( bstr );
    LPSTR str;

    len = WideCharToMultiByte( CP_UTF8, 0, bstr, blen, NULL, 0, NULL, NULL );
    str = HeapAlloc( GetProcessHeap(), 0, len );
    if ( !str )
        return FALSE;
    WideCharToMultiByte( CP_UTF8, 0, bstr, blen, str, len, NULL, NULL );
    *plen = len;
    *pstr = str;
    return TRUE;
}

static HRESULT WINAPI domdoc_loadXML(
    IXMLDOMDocument2 *iface,
    BSTR bstrXML,
    VARIANT_BOOL* isSuccessful )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    xmlDocPtr xmldoc = NULL;
    char *str;
    int len;
    HRESULT hr = S_FALSE;

    TRACE("%p %s %p\n", This, debugstr_w( bstrXML ), isSuccessful );

    assert ( This->node );

    attach_xmlnode( This->node, NULL );

    if ( isSuccessful )
    {
        *isSuccessful = VARIANT_FALSE;

        if ( bstrXML  && bstr_to_utf8( bstrXML, &str, &len ) )
        {
            xmldoc = doparse( str, len );
            HeapFree( GetProcessHeap(), 0, str );
            if ( !xmldoc )
                This->error = E_FAIL;
            else
            {
                hr = This->error = S_OK;
                *isSuccessful = VARIANT_TRUE;
            }
        }
    }
    if(!xmldoc)
        xmldoc = xmlNewDoc(NULL);

    xmldoc->_private = 0;
    attach_xmlnode( This->node, (xmlNodePtr) xmldoc );

    return hr;
}


static HRESULT WINAPI domdoc_save(
    IXMLDOMDocument2 *iface,
    VARIANT destination )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    HANDLE handle;
    xmlChar *mem;
    int size;
    HRESULT ret = S_OK;
    DWORD written;

    TRACE("(%p)->(var(vt %x, %s))\n", This, V_VT(&destination),
          V_VT(&destination) == VT_BSTR ? debugstr_w(V_BSTR(&destination)) : NULL);

    if(V_VT(&destination) != VT_BSTR)
    {
        FIXME("Unhandled vt %x\n", V_VT(&destination));
        return S_FALSE;
    }

    handle = CreateFileW( V_BSTR(&destination), GENERIC_WRITE, 0,
                          NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if( handle == INVALID_HANDLE_VALUE )
    {
        WARN("failed to create file\n");
        return S_FALSE;
    }

    xmlDocDumpMemory(get_doc(This), &mem, &size);
    if(!WriteFile(handle, mem, (DWORD)size, &written, NULL) || written != (DWORD)size)
    {
        WARN("write error\n");
        ret = S_FALSE;
    }

    xmlFree(mem);
    CloseHandle(handle);
    return ret;
}

static HRESULT WINAPI domdoc_get_validateOnParse(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isValidating )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%p <- %d\n", isValidating, This->validating);
    *isValidating = This->validating;
    return S_OK;
}


static HRESULT WINAPI domdoc_put_validateOnParse(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL isValidating )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%d\n", isValidating);
    This->validating = isValidating;
    return S_OK;
}


static HRESULT WINAPI domdoc_get_resolveExternals(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isResolving )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%p <- %d\n", isResolving, This->resolving);
    *isResolving = This->resolving;
    return S_OK;
}


static HRESULT WINAPI domdoc_put_resolveExternals(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL isResolving )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%d\n", isResolving);
    This->resolving = isResolving;
    return S_OK;
}


static HRESULT WINAPI domdoc_get_preserveWhiteSpace(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL* isPreserving )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%p <- %d\n", isPreserving, This->preserving);
    *isPreserving = This->preserving;
    return S_OK;
}


static HRESULT WINAPI domdoc_put_preserveWhiteSpace(
    IXMLDOMDocument2 *iface,
    VARIANT_BOOL isPreserving )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );

    TRACE("%d\n", isPreserving);
    This->preserving = isPreserving;
    return S_OK;
}


static HRESULT WINAPI domdoc_put_onReadyStateChange(
    IXMLDOMDocument2 *iface,
    VARIANT readyStateChangeSink )
{
    FIXME("\n");
    return E_NOTIMPL;
}


static HRESULT WINAPI domdoc_put_onDataAvailable(
    IXMLDOMDocument2 *iface,
    VARIANT onDataAvailableSink )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_put_onTransformNode(
    IXMLDOMDocument2 *iface,
    VARIANT onTransformNodeSink )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_get_namespaces(
    IXMLDOMDocument2* iface,
    IXMLDOMSchemaCollection** schemaCollection )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_get_schemas(
    IXMLDOMDocument2* iface,
    VARIANT* var1 )
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    HRESULT hr = S_FALSE;
    IXMLDOMSchemaCollection *cur_schema = This->schema;

    TRACE("(%p)->(%p)\n", This, var1);

    VariantInit(var1); /* Test shows we don't call VariantClear here */
    V_VT(var1) = VT_NULL;

    if(cur_schema)
    {
        hr = IXMLDOMSchemaCollection_QueryInterface(cur_schema, &IID_IDispatch, (void**)&V_DISPATCH(var1));
        if(SUCCEEDED(hr))
            V_VT(var1) = VT_DISPATCH;
    }
    return hr;
}

static HRESULT WINAPI domdoc_putref_schemas(
    IXMLDOMDocument2* iface,
    VARIANT var1)
{
    domdoc *This = impl_from_IXMLDOMDocument2( iface );
    HRESULT hr = E_FAIL;
    IXMLDOMSchemaCollection *new_schema = NULL;

    FIXME("(%p): semi-stub\n", This);
    switch(V_VT(&var1))
    {
    case VT_UNKNOWN:
        hr = IUnknown_QueryInterface(V_UNKNOWN(&var1), &IID_IXMLDOMSchemaCollection, (void**)&new_schema);
        break;

    case VT_DISPATCH:
        hr = IDispatch_QueryInterface(V_DISPATCH(&var1), &IID_IXMLDOMSchemaCollection, (void**)&new_schema);
        break;

    case VT_NULL:
    case VT_EMPTY:
        hr = S_OK;
        break;

    default:
        WARN("Can't get schema from vt %x\n", V_VT(&var1));
    }

    if(SUCCEEDED(hr))
    {
        IXMLDOMSchemaCollection *old_schema = InterlockedExchangePointer((void**)&This->schema, new_schema);
        if(old_schema) IXMLDOMSchemaCollection_Release(old_schema);
    }

    return hr;
}

static HRESULT WINAPI domdoc_validate(
    IXMLDOMDocument2* iface,
    IXMLDOMParseError** err)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_setProperty(
    IXMLDOMDocument2* iface,
    BSTR p,
    VARIANT var)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI domdoc_getProperty(
    IXMLDOMDocument2* iface,
    BSTR p,
    VARIANT* var)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static const struct IXMLDOMDocument2Vtbl domdoc_vtbl =
{
    domdoc_QueryInterface,
    domdoc_AddRef,
    domdoc_Release,
    domdoc_GetTypeInfoCount,
    domdoc_GetTypeInfo,
    domdoc_GetIDsOfNames,
    domdoc_Invoke,
    domdoc_get_nodeName,
    domdoc_get_nodeValue,
    domdoc_put_nodeValue,
    domdoc_get_nodeType,
    domdoc_get_parentNode,
    domdoc_get_childNodes,
    domdoc_get_firstChild,
    domdoc_get_lastChild,
    domdoc_get_previousSibling,
    domdoc_get_nextSibling,
    domdoc_get_attributes,
    domdoc_insertBefore,
    domdoc_replaceChild,
    domdoc_removeChild,
    domdoc_appendChild,
    domdoc_hasChildNodes,
    domdoc_get_ownerDocument,
    domdoc_cloneNode,
    domdoc_get_nodeTypeString,
    domdoc_get_text,
    domdoc_put_text,
    domdoc_get_specified,
    domdoc_get_definition,
    domdoc_get_nodeTypedValue,
    domdoc_put_nodeTypedValue,
    domdoc_get_dataType,
    domdoc_put_dataType,
    domdoc_get_xml,
    domdoc_transformNode,
    domdoc_selectNodes,
    domdoc_selectSingleNode,
    domdoc_get_parsed,
    domdoc_get_namespaceURI,
    domdoc_get_prefix,
    domdoc_get_baseName,
    domdoc_transformNodeToObject,
    domdoc_get_doctype,
    domdoc_get_implementation,
    domdoc_get_documentElement,
    domdoc_documentElement,
    domdoc_createElement,
    domdoc_createDocumentFragment,
    domdoc_createTextNode,
    domdoc_createComment,
    domdoc_createCDATASection,
    domdoc_createProcessingInstruction,
    domdoc_createAttribute,
    domdoc_createEntityReference,
    domdoc_getElementsByTagName,
    domdoc_createNode,
    domdoc_nodeFromID,
    domdoc_load,
    domdoc_get_readyState,
    domdoc_get_parseError,
    domdoc_get_url,
    domdoc_get_async,
    domdoc_put_async,
    domdoc_abort,
    domdoc_loadXML,
    domdoc_save,
    domdoc_get_validateOnParse,
    domdoc_put_validateOnParse,
    domdoc_get_resolveExternals,
    domdoc_put_resolveExternals,
    domdoc_get_preserveWhiteSpace,
    domdoc_put_preserveWhiteSpace,
    domdoc_put_onReadyStateChange,
    domdoc_put_onDataAvailable,
    domdoc_put_onTransformNode,
    domdoc_get_namespaces,
    domdoc_get_schemas,
    domdoc_putref_schemas,
    domdoc_validate,
    domdoc_setProperty,
    domdoc_getProperty
};

HRESULT DOMDocument_create(IUnknown *pUnkOuter, LPVOID *ppObj)
{
    domdoc *doc;
    HRESULT hr;
    xmlDocPtr xmldoc;

    TRACE("(%p,%p)\n", pUnkOuter, ppObj);

    doc = HeapAlloc( GetProcessHeap(), 0, sizeof (*doc) );
    if( !doc )
        return E_OUTOFMEMORY;

    doc->lpVtbl = &domdoc_vtbl;
    doc->ref = 1;
    doc->async = 0;
    doc->validating = 0;
    doc->resolving = 0;
    doc->preserving = 0;
    doc->error = S_OK;
    doc->schema = NULL;

    xmldoc = xmlNewDoc(NULL);
    if(!xmldoc)
    {
        HeapFree(GetProcessHeap(), 0, doc);
        return E_OUTOFMEMORY;
    }

    xmldoc->_private = 0;

    doc->node_unk = create_basic_node( (xmlNodePtr)xmldoc, (IUnknown*)&doc->lpVtbl );
    if(!doc->node_unk)
    {
        xmlFreeDoc(xmldoc);
        HeapFree(GetProcessHeap(), 0, doc);
        return E_FAIL;
    }

    hr = IUnknown_QueryInterface(doc->node_unk, &IID_IXMLDOMNode, (LPVOID*)&doc->node);
    if(FAILED(hr))
    {
        IUnknown_Release(doc->node_unk);
        HeapFree( GetProcessHeap(), 0, doc );
        return E_FAIL;
    }
    /* The ref on doc->node is actually looped back into this object, so release it */
    IXMLDOMNode_Release(doc->node);

    *ppObj = &doc->lpVtbl;

    TRACE("returning iface %p\n", *ppObj);
    return S_OK;
}

#else

HRESULT DOMDocument_create(IUnknown *pUnkOuter, LPVOID *ppObj)
{
    MESSAGE("This program tried to use a DOMDocument object, but\n"
            "libxml2 support was not present at compile time.\n");
    return E_NOTIMPL;
}

#endif
