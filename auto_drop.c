//
// "$Id: auto_drop.c 19928 2018-09-15 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// auto_drop.c extends the Windows Edit control with autocompletion
// plus enabling text drop target function for scripting 
//
// Copyright 2015-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/tinyTerm/issues/new
//
#define INITGUID
#include <basetyps.h>
#include <windows.h>
#include <ole2.h>
#include <shlwapi.h>
#include <shldisp.h>

//DEFINE_GUID(IID_IDropSource,0x121,0,0,0xc0,0,0,0,0,0,0,0x46);
//DEFINE_GUID(IID_IDropTarget,0x122,0,0,0xc0,0,0,0,0,0,0,0x46);
DEFINE_GUID(CLSID_AutoComplete,  0x00bb2763, 0x6a77, 0x11d0, 0xa5, 0x35, 0x00, 0xc0, 0x4f, 0xd7, 0xd0, 0x62);
#define ACO_NONE			 0x0
#define ACO_AUTOSUGGEST		 0x1
#define ACO_AUTOAPPEND		 0x2
#define ACO_SEARCH			 0x4
#define ACO_FILTERPREFIXES	 0x8
#define ACO_USETAB			 0x10
#define ACO_UPDOWNKEYDROPSLIST	0x20

#define STRINGSIZE 256
/**************************************************************************
	class CAutoEnumString
	Description: to impletement the IEnumString interface, 
	then provided to IAutoCompelte for command history
**************************************************************************/
typedef struct CAutoEnumString {
    IEnumStringVtbl *lpVtbl;
	LPOLESTR * m_arString;
	unsigned int m_Size;
	unsigned int m_iterCur;
	unsigned int m_addCur;
	unsigned int m_rtrvCur;
	int	m_Ref;
} CAutoEnumString;

void CAutoEnumString_Construct(CAutoEnumString* this);
void CAutoEnumString_Destruct(CAutoEnumString* this);
int CAutoEnumString_AddString(CAutoEnumString* this, LPOLESTR lpszStr);

STDMETHODIMP_(ULONG) CAutoEnumString_AddRef(IEnumString *this)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	return ++self->m_Ref;
}

STDMETHODIMP_(ULONG) CAutoEnumString_Release(IEnumString *this)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	return  --self->m_Ref;
}

STDMETHODIMP CAutoEnumString_QueryInterface(IEnumString *this, REFIID iid, void **ppv)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	if ( IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IEnumString) ){
		*ppv = (IEnumString *) this ;
		self->m_Ref ++;
	}
	else{
		*ppv = NULL;
		return E_NOINTERFACE ;
	}
	return S_OK;
}

STDMETHODIMP CAutoEnumString_Next(IEnumString *this, ULONG celt, LPOLESTR *rgelt, ULONG *pceltFetched)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	if (rgelt == NULL || (celt != 1 && pceltFetched == NULL)) return E_POINTER;
    if (self->m_iterCur == self->m_addCur) return E_FAIL;

    ULONG nFetched = 0;
    while( self->m_iterCur<self->m_addCur && celt>0) {
		(*rgelt) = (LPOLESTR)CoTaskMemAlloc(sizeof(OLECHAR)*STRINGSIZE);
		if ( *rgelt ) {
			wcscpy(*rgelt, self->m_arString[self->m_iterCur++]);
			celt--; nFetched++; rgelt++;
		}
		else break;
	}
 	if (pceltFetched != NULL) *pceltFetched = nFetched;

    return (celt>0)? S_FALSE:S_OK;
}

STDMETHODIMP CAutoEnumString_Skip(IEnumString *this, ULONG celt)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	if (self->m_addCur - self->m_iterCur <  celt) {
		self->m_iterCur = self->m_addCur;
		return S_FALSE;
	}
	self->m_iterCur += celt;
	return S_OK;
}

STDMETHODIMP CAutoEnumString_Reset(IEnumString *this)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	self->m_iterCur = 0;
	return S_OK;
}

STDMETHODIMP CAutoEnumString_Clone(IEnumString *this, IEnumString** ppEnum)
{
	return  E_POINTER;
}

IEnumStringVtbl CAutoEnumStringVtbl = {
	CAutoEnumString_QueryInterface,
	&CAutoEnumString_AddRef,
	&CAutoEnumString_Release,
	CAutoEnumString_Next,
	CAutoEnumString_Skip,
	CAutoEnumString_Reset,
	CAutoEnumString_Clone,
};

void CAutoEnumString_Construct(CAutoEnumString* this)
{
	this->lpVtbl = &CAutoEnumStringVtbl;
	this->m_arString = (LPOLESTR *)malloc(256*sizeof(LPOLESTR));
	this->m_Size = 256;
	this->m_iterCur = 0;
	this->m_addCur = 0;
	this->m_rtrvCur = 0;
}
void CAutoEnumString_Destruct(CAutoEnumString* this)
{
	if ( this->m_addCur>0 )
		for (int i=0; i<this->m_addCur; i++)
			free(this->m_arString[i]);
	if ( this->m_arString ) free(this->m_arString);
}
int CAutoEnumString_AddString(CAutoEnumString* this, LPOLESTR lpszStr)
{
	for ( int i=0; i<this->m_addCur; i++ ) 
		if ( wcscmp(this->m_arString[i], lpszStr) == 0 ) 
			return 0;

	this->m_arString[this->m_addCur] = _wcsdup(lpszStr);
	this->m_rtrvCur = this->m_addCur;

	if ( ++this->m_addCur==this->m_Size ) {
		int size = this->m_Size*2;
		LPOLESTR * arString = realloc(this->m_arString, size*sizeof(LPOLESTR));
		if ( arString != NULL ) {
			this->m_arString=arString;
			this->m_Size=size;
		}
		else
			this->m_addCur--;
	}
	return 1;
}
LPOLESTR CAutoEnumString_prevString(CAutoEnumString* this)
{
    if ( this->m_rtrvCur>0 ) this->m_rtrvCur--;
    return this->m_arString[this->m_rtrvCur];
}
LPOLESTR CAutoEnumString_nextString(CAutoEnumString* this)
{
    if ( ++this->m_rtrvCur==this->m_addCur ) this->m_rtrvCur--;
    return this->m_arString[this->m_rtrvCur];
}
/************************************************************************
	AutoComplete implementation
************************************************************************/
static CAutoEnumString cmdHistory;
static IAutoComplete *pauto=NULL;
static IAutoComplete2 *pauto2=NULL;
static IEnumString *penum=NULL;
static HWND m_hwnd;

void autocomplete_Init(HWND hwndCmd)
{
	m_hwnd=hwndCmd;
	CoCreateInstance( &CLSID_AutoComplete, NULL, CLSCTX_INPROC_SERVER,
		&IID_IAutoComplete,(LPVOID *) &pauto );

	if ( pauto ) {
		pauto->lpVtbl->QueryInterface(pauto, &IID_IAutoComplete2, (PVOID*)&pauto2);
		if ( pauto2 ) {
			pauto2->lpVtbl->SetOptions(pauto2, ACO_AUTOSUGGEST|ACO_AUTOAPPEND );
			pauto2->lpVtbl->Release(pauto2);
		}
		CAutoEnumString_Construct( &cmdHistory );
		cmdHistory.lpVtbl->QueryInterface((IEnumString *)&cmdHistory, &IID_IEnumString, (PVOID*)&penum);
		if ( penum ) pauto->lpVtbl->Init(pauto, m_hwnd, (IUnknown *)penum, NULL, NULL);
	}
}
int autocomplete_Add(LPOLESTR cmd)
{
	return ( pauto && penum ) ? CAutoEnumString_AddString(&cmdHistory, cmd) : 0;
}
LPOLESTR autocomplete_Prev( void )
{
	return CAutoEnumString_prevString(&cmdHistory);
}
LPOLESTR autocomplete_Next( void )
{
	return CAutoEnumString_nextString(&cmdHistory);
}
void autocomplete_Destroy()
{
	if ( pauto ) pauto->lpVtbl->Release(pauto);
	if ( penum ) penum->lpVtbl->Release(penum);
	CAutoEnumString_Destruct(&cmdHistory);
}

void tiny_Drop_Script( char *cmds );

/************************************************************************
	DropTarget implementation
************************************************************************/
// data object:
typedef struct {
	IDataObject ido;
	int ref_count;
	FORMATETC *m_pFormatEtc;
	STGMEDIUM *m_pStgMedium;
	LONG	   m_nNumFormats;
	LONG	   m_lRefCount;
} WB_IDataObject;


typedef struct {
	IEnumFORMATETC ief;
	int ref_count;
	int ix;
	LONG		m_lRefCount;		// Reference count for this COM interface
	ULONG		m_nIndex;			// current enumerator index
	ULONG		m_nNumFormats;		// number of FORMATETC members
	FORMATETC * m_pFormatEtc;
} WB_IEnumFORMATETC;

typedef struct {
	IDropSource ids;
	LONG	m_lRefCount;
} WB_IDropSource;

typedef struct {
	IDropTarget idt;
	LONG	m_lRefCount;
	HWND	m_hWnd;
	BOOL	m_fAllowDrop;
	IDataObject *m_pDataObject;
} WB_IDropTarget;

typedef struct WB_IDataObjectVtbl
	{
		BEGIN_INTERFACE
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
			WB_IDataObject __RPC_FAR * This,
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
			WB_IDataObject __RPC_FAR * This);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
			WB_IDataObject __RPC_FAR * This);
		
		/* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetData )( 
			WB_IDataObject __RPC_FAR * This,
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetcIn,
			/* [out] */ STGMEDIUM __RPC_FAR *pmedium);
		
		/* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetDataHere )( 
			WB_IDataObject __RPC_FAR * This,
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [out][in] */ STGMEDIUM __RPC_FAR *pmedium);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryGetData )( 
			WB_IDataObject __RPC_FAR * This,
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetCanonicalFormatEtc )( 
			WB_IDataObject __RPC_FAR * This,
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatectIn,
			/* [out] */ FORMATETC __RPC_FAR *pformatetcOut);
		
		/* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SetData )( 
			WB_IDataObject __RPC_FAR * This,
			/* [unique][in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [unique][in] */ STGMEDIUM __RPC_FAR *pmedium,
			/* [in] */ BOOL fRelease);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *EnumFormatEtc )( 
			WB_IDataObject __RPC_FAR * This,
			/* [in] */ DWORD dwDirection,
			/* [out] */ IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenumFormatEtc);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DAdvise )( 
			WB_IDataObject __RPC_FAR * This,
			/* [in] */ FORMATETC __RPC_FAR *pformatetc,
			/* [in] */ DWORD advf,
			/* [unique][in] */ IAdviseSink __RPC_FAR *pAdvSink,
			/* [out] */ DWORD __RPC_FAR *pdwConnection);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DUnadvise )( 
			WB_IDataObject __RPC_FAR * This,
			/* [in] */ DWORD dwConnection);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *EnumDAdvise )( 
			WB_IDataObject __RPC_FAR * This,
			/* [out] */ IEnumSTATDATA __RPC_FAR *__RPC_FAR *ppenumAdvise);
		
		END_INTERFACE
	} WB_IDataObjectVtbl;

typedef struct WB_IEnumFORMATETCVtbl
	{
		BEGIN_INTERFACE
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
			WB_IEnumFORMATETC __RPC_FAR * This,
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
			WB_IEnumFORMATETC __RPC_FAR * This);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
			WB_IEnumFORMATETC __RPC_FAR * This);
		
		/* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Next )( 
			WB_IEnumFORMATETC __RPC_FAR * This,
			/* [in] */ ULONG celt,
			/* [length_is][size_is][out] */ FORMATETC __RPC_FAR *rgelt,
			/* [out] */ ULONG __RPC_FAR *pceltFetched);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Skip )( 
			WB_IEnumFORMATETC __RPC_FAR * This,
			/* [in] */ ULONG celt);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Reset )( 
			WB_IEnumFORMATETC __RPC_FAR * This);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Clone )( 
			WB_IEnumFORMATETC __RPC_FAR * This,
			/* [out] */ WB_IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenum);
		
		END_INTERFACE
	} WB_IEnumFORMATETCVtbl;


typedef struct WB_IDropSourceVtbl
	{
		BEGIN_INTERFACE
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
			WB_IDropSource __RPC_FAR * This,
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
			WB_IDropSource __RPC_FAR * This);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
			WB_IDropSource __RPC_FAR * This);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryContinueDrag )( 
			WB_IDropSource __RPC_FAR * This,
			/* [in] */ BOOL fEscapePressed,
			/* [in] */ DWORD grfKeyState);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GiveFeedback )( 
			WB_IDropSource __RPC_FAR * This,
			/* [in] */ DWORD dwEffect);
		
		END_INTERFACE
	} WB_IDropSourceVtbl;

typedef struct WB_IDropTargetVtbl
	{
		BEGIN_INTERFACE
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
			WB_IDropTarget __RPC_FAR * This,
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
			WB_IDropTarget __RPC_FAR * This);
		
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
			WB_IDropTarget __RPC_FAR * This);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragEnter )( 
			WB_IDropTarget __RPC_FAR * This,
			/* [unique][in] */ WB_IDataObject __RPC_FAR *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragOver )( 
			WB_IDropTarget __RPC_FAR * This,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragLeave )( 
			WB_IDropTarget __RPC_FAR * This);
		
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Drop )( 
			WB_IDropTarget __RPC_FAR * This,
			/* [unique][in] */ IDataObject __RPC_FAR *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ DWORD __RPC_FAR *pdwEffect);
		
		END_INTERFACE
	} WB_IDropTargetVtbl;

typedef WB_IDataObject* LPWBDATAOBJECT;
typedef WB_IEnumFORMATETC* LPWBFORMATETC;
WB_IEnumFORMATETC *WB_IEnumFORMATETC_new (UINT, FORMATETC *);

static IDropTarget *pDropTarget;
HRESULT CreateDropTarget(HWND hwnd, WB_IDropTarget **ppDropTarget);
void DropData(HWND hwnd, IDataObject *pDataObject);
//
//	Position the edit control's caret under the mouse
//
void PositionCursor(HWND hwndEdit, POINTL pt)
{
	DWORD curpos; 

	// get the character position of mouse
	ScreenToClient(hwndEdit, (POINT *)&pt);
	curpos = SendMessage(hwndEdit, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));

	// set cursor position
	SendMessage(hwndEdit, EM_SETSEL, LOWORD(curpos), LOWORD(curpos));
}

//
//	QueryDataObject private helper routine
//
static BOOL QueryDataObject (WB_IDataObject *pDataObject)
{
	FORMATETC fmtetc = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

	// does the data object support CF_TEXT using a HGLOBAL?
	return pDataObject->ido.lpVtbl->QueryGetData((LPDATAOBJECT)pDataObject, &fmtetc) == S_OK ? TRUE : FALSE;
}

//
//	DropEffect private helper routine
//
static DWORD DropEffect(DWORD grfKeyState, POINTL pt, DWORD dwAllowed)
{
	DWORD dwEffect = 0;

	// 1. check "pt" -> do we allow a drop at the specified coordinates?
	
	// 2. work out that the drop-effect should be based on grfKeyState
	if(grfKeyState & MK_CONTROL)
	{
		dwEffect = dwAllowed & DROPEFFECT_COPY;
	}
//	else if(grfKeyState & MK_SHIFT)
//	{
//		dwEffect = dwAllowed & DROPEFFECT_MOVE;
//	}
	
	// 3. no key-modifiers were specified (or drop effect not allowed), so
	//    base the effect on those allowed by the dropsource
	if(dwEffect == 0)
	{
		if(dwAllowed & DROPEFFECT_COPY) dwEffect = DROPEFFECT_COPY;
//		if(dwAllowed & DROPEFFECT_MOVE) dwEffect = DROPEFFECT_MOVE;
	}
	
	return dwEffect;
}

//
//	IUnknown::AddRef
//
static ULONG STDMETHODCALLTYPE idroptarget_addref (WB_IDropTarget* This)
{
	return InterlockedIncrement(&This->m_lRefCount);
}

//
//	IUnknown::QueryInterface
//
static HRESULT STDMETHODCALLTYPE
idroptarget_queryinterface (WB_IDropTarget *This,
			       REFIID          riid,
			       LPVOID         *ppvObject)
{
	*ppvObject = NULL;

//  PRINT_GUID (riid);
	if (IsEqualIID (riid, &IID_IUnknown) || IsEqualIID (riid, &IID_IDropTarget))
	{
		idroptarget_addref (This);
		*ppvObject = This;
		return S_OK;
	}
	else
	{
		return E_NOINTERFACE;
	}
}
//
//	IUnknown::Release
//
static ULONG STDMETHODCALLTYPE
idroptarget_release (WB_IDropTarget* This)
{

	LONG count = InterlockedDecrement(&This->m_lRefCount);

	if(count == 0)
	{
		LocalFree(This); //g_free
		return 0;
	}
	else
	{
		return count;
	}
}
//
//	IDropTarget::DragEnter
//
//
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragenter(WB_IDropTarget* This, WB_IDataObject * pDataObject, DWORD grfKeyState, POINTL pt, DWORD * pdwEffect)
{
	// does the dataobject contain data we want?
	This->m_fAllowDrop = QueryDataObject(pDataObject);
	
	if(This->m_fAllowDrop)
	{
		// get the dropeffect based on keyboard state
		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);

		SetFocus(This->m_hWnd);

		PositionCursor(This->m_hWnd, pt);
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

//
//	IDropTarget::DragOver
//
//
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragover(WB_IDropTarget* This, DWORD grfKeyState, POINTL pt, DWORD * pdwEffect)
{
	if(This->m_fAllowDrop)
	{
		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);
		PositionCursor(This->m_hWnd, pt);
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

//
//	IDropTarget::DragLeave
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragleave(WB_IDropTarget* This)
{
	return S_OK;
}

//
//	IDropTarget::Drop
//
//
static HRESULT STDMETHODCALLTYPE idroptarget_drop(WB_IDropTarget* This, IDataObject * pDataObject, DWORD grfKeyState, POINTL pt, DWORD * pdwEffect)
{
	PositionCursor(This->m_hWnd, pt);

	if(This->m_fAllowDrop)
	{
		DropData(This->m_hWnd, pDataObject);

		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

static WB_IDropTargetVtbl idt_vtbl = {
  idroptarget_queryinterface,
  idroptarget_addref,
  idroptarget_release,
  idroptarget_dragenter,
  idroptarget_dragover,
  idroptarget_dragleave,
  idroptarget_drop
};

void DropData(HWND hwnd, IDataObject *pDataObject)
{
	// construct a FORMATETC object
	FORMATETC fmtetc = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM stgmed;

	// See if the dataobject contains any TEXT stored as a HGLOBAL
	if(pDataObject->lpVtbl->QueryGetData(pDataObject, &fmtetc) == S_OK)
	{
		// Yippie! the data is there, so go get it!
		if(pDataObject->lpVtbl->GetData(pDataObject, &fmtetc, &stgmed) == S_OK)
		{
			// we asked for the data as a HGLOBAL, so access it appropriately
			PVOID data = GlobalLock(stgmed.hGlobal);

			tiny_Drop_Script(strdup((char *)data));

			GlobalUnlock(stgmed.hGlobal);

			// release the data using the COM API
			ReleaseStgMedium(&stgmed);
		}
	}
}

void RegisterDropWindow(HWND hwnd, WB_IDropTarget **ppDropTarget)
{
	WB_IDropTarget *pDropTarget;
	
	CreateDropTarget(hwnd, &pDropTarget);

	// acquire a strong lock
	CoLockObjectExternal((struct IUnknown*)pDropTarget, TRUE, FALSE);

	// tell OLE that the window is a drop target
	RegisterDragDrop(hwnd, (LPDROPTARGET)pDropTarget);

	*ppDropTarget = pDropTarget;
}

void UnregisterDropWindow(HWND hwnd, IDropTarget *pDropTarget)
{
	// remove drag+drop
	RevokeDragDrop(hwnd);

	// remove the strong lock
	CoLockObjectExternal((struct IUnknown*)pDropTarget, FALSE, TRUE);

	// release our own reference
	pDropTarget->lpVtbl->Release(pDropTarget);
}

//	Constructor for the CDropTarget class
//

WB_IDropTarget * WB_IDropTarget_new(HWND hwnd)
{
  WB_IDropTarget *result;

  result = (WB_IDropTarget *)LocalAlloc(LPTR, sizeof(WB_IDropTarget));

  result->idt.lpVtbl = (IDropTargetVtbl*)&idt_vtbl;

  result->m_lRefCount  = 1;
  result->m_hWnd = hwnd;
  result->m_fAllowDrop = FALSE;
  
  return result;
}

HRESULT CreateDropTarget(HWND hwnd, WB_IDropTarget **ppDropTarget) 
{
	if(ppDropTarget == 0)
		return E_INVALIDARG;

	*ppDropTarget = WB_IDropTarget_new(hwnd);

	return (*ppDropTarget) ? S_OK : E_OUTOFMEMORY;

}
void drop_Init( HWND hwnd )
{
  RegisterDropWindow(hwnd, (WB_IDropTarget **)&pDropTarget);
}
void drop_Destroy( HWND hwnd )
{
  UnregisterDropWindow(hwnd, pDropTarget);
}