//
// "$Id: auto_drop.c 17982 2019-05-09 23:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// auto_drop.c implements COM interfaces for
// edit control autocompletion and text window drop target
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//		https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//		https://github.com/yongchaofan/tinyTerm/issues/new
//
#define INITGUID
#include <basetyps.h>
#include <windows.h>
#include <ole2.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <shlguid.h>
//DEFINE_GUID(CLSID_AutoComplete, 0x00bb2763, 0x6a77, 0x11d0, 0xa5, 0x35,
//									0x00, 0xc0, 0x4f, 0xd7, 0xd0, 0x62);
//
//impletemention of IEnumString interface,
//provided to IAutoCompelte for command history
//
typedef struct CAutoEnumString {
    IEnumStringVtbl *lpVtbl;
	LPOLESTR * m_arString;
	unsigned int m_Size;
	unsigned int m_iterCur;
	unsigned int m_count;
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
	return --self->m_Ref;
}

STDMETHODIMP CAutoEnumString_QueryInterface(IEnumString *this,
											REFIID iid, void **ppv)
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

STDMETHODIMP CAutoEnumString_Next(IEnumString *this, ULONG celt,
									LPOLESTR *rgelt, ULONG *pceltFetched)
{
	CAutoEnumString *self = (CAutoEnumString *)this;
	if (rgelt == NULL || (celt != 1 && pceltFetched == NULL)) return E_POINTER;
	if (self->m_iterCur == self->m_count) return E_FAIL;

	ULONG nFetched = 0;
	while( self->m_iterCur<self->m_count && celt>0) {
		(*rgelt) = (LPOLESTR)CoTaskMemAlloc(sizeof(OLECHAR)*256);
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
	if (self->m_count - self->m_iterCur <  celt) {
		self->m_iterCur = self->m_count;
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
	return E_POINTER;
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
	this->m_count = 0;
	this->m_rtrvCur = 0;
}
void CAutoEnumString_Destruct(CAutoEnumString* this)
{
	if ( this->m_count>0 )
		for (int i=0; i<this->m_count; i++) free(this->m_arString[i]);
	if ( this->m_arString ) free(this->m_arString);
}
int CAutoEnumString_AddString(CAutoEnumString* this, LPOLESTR lpszStr)
{
	int cur;
	for ( cur=0; cur<this->m_count; cur++ ) {
		int cmp =  wcscmp(this->m_arString[cur], lpszStr);
		if ( cmp == 0 ) return 0;
		if ( cmp > 0 ) break;
	} 
	if ( ++this->m_count==this->m_Size ) {
		int size = this->m_Size*2;
		LPOLESTR * arString = realloc(this->m_arString, size*sizeof(LPOLESTR));
		if ( arString != NULL ) {
			this->m_arString=arString;
			this->m_Size=size;
		}
		else
			this->m_count--;
	}
	for ( int i=this->m_count-1; i>=cur; i-- ) 
		this->m_arString[i+1] = this->m_arString[i];
	this->m_arString[cur] = _wcsdup(lpszStr);
	this->m_rtrvCur = cur;

	return 1;
}
int CAutoEnumString_DelString(CAutoEnumString* this, LPOLESTR lpszStr)
{
	for ( int cur=0; cur<this->m_count; cur++ ) {
		if ( wcscmp(this->m_arString[cur], lpszStr) == 0 ) {
			free(this->m_arString[cur]);
			this->m_arString[cur] = _wcsdup(L"");
			return this->m_rtrvCur = cur;
		}
	} 
	return 0;
}
LPOLESTR CAutoEnumString_prevString(CAutoEnumString* this)
{
	if ( this->m_count>0 && this->m_rtrvCur>0)
		return this->m_arString[this->m_rtrvCur--];
	else
		return NULL;
}
LPOLESTR CAutoEnumString_firstString(CAutoEnumString* this)
{
	this->m_rtrvCur = 0;
	if ( this->m_count>0 ) 
		return this->m_arString[0];
	else
		return NULL;
}
LPOLESTR CAutoEnumString_nextString(CAutoEnumString* this)
{
	if ( this->m_rtrvCur < this->m_count-1 )
		return this->m_arString[++this->m_rtrvCur];
	else 
		return NULL;
}
//
//	AutoComplete implementation
//
static CAutoEnumString cmdHistory;
static IEnumString *penum  =NULL;
static IAutoComplete *pauto = NULL;

void autocomplete_Init(HWND hwndCmd)
{
	CoCreateInstance( &CLSID_AutoComplete, NULL, CLSCTX_INPROC_SERVER,
		&IID_IAutoComplete,(LPVOID *) &pauto );

	if ( pauto ) {
		IAutoComplete2 *pauto2 = NULL;
		pauto->lpVtbl->QueryInterface(pauto,&IID_IAutoComplete2,(PVOID*)&pauto2);
		if ( pauto2 ) {
			pauto2->lpVtbl->SetOptions(pauto2, ACO_AUTOSUGGEST|ACO_AUTOAPPEND);
			pauto2->lpVtbl->Release(pauto2);
		}
		CAutoEnumString_Construct( &cmdHistory );
		cmdHistory.lpVtbl->QueryInterface( (IEnumString *)&cmdHistory,
											&IID_IEnumString, (PVOID*)&penum);
		if ( penum )
			pauto->lpVtbl->Init(pauto, hwndCmd, (IUnknown *)penum, NULL, NULL);
	}
}
int autocomplete_Add(LPOLESTR cmd)
{
	if ( pauto && penum && cmd ) {
		if ( *cmd ) 
			return CAutoEnumString_AddString(&cmdHistory, cmd);
	}
	return 0;
}
int autocomplete_Del(LPOLESTR cmd)
{
	if ( pauto && penum && cmd ) {
		if ( *cmd ) 
			return CAutoEnumString_DelString(&cmdHistory, cmd);
	}
	return 0;
}
LPOLESTR autocomplete_First( void )
{
	return CAutoEnumString_firstString(&cmdHistory);
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

//
//	DropTarget implementation
//
typedef struct {
	IDataObject ido;
	int ref_count;
	FORMATETC *m_pFormatEtc;
	STGMEDIUM *m_pStgMedium;
	LONG m_nNumFormats;
	LONG m_lRefCount;
} WB_IDataObject;

typedef struct {
	IEnumFORMATETC ief;
	int ref_count;
	int ix;
	LONG m_lRefCount;
	ULONG m_nIndex;
	ULONG m_nNumFormats;
	FORMATETC * m_pFormatEtc;
} WB_IEnumFORMATETC;

typedef struct {
	IDropSource ids;
	LONG m_lRefCount;
} WB_IDropSource;

typedef struct {
	IDropTarget idt;
	LONG m_lRefCount;
	HWND m_hWnd;
	BOOL m_fAllowDrop;
	IDataObject *m_pDataObject;
} WB_IDropTarget;

typedef struct WB_IDataObjectVtbl
{
	BEGIN_INTERFACE

	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )(
		WB_IDataObject __RPC_FAR * This,
		REFIID riid,
		void __RPC_FAR *__RPC_FAR *ppvObject				// [iid_is][out]
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )(
		WB_IDataObject __RPC_FAR * This);
		ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )(
		WB_IDataObject __RPC_FAR * This);
		HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetData )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatetcIn,					// [unique][in]
		STGMEDIUM __RPC_FAR *pmedium						// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetDataHere )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatetc,					// [unique][in]
		STGMEDIUM __RPC_FAR *pmedium						// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryGetData )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatetc						// [unique][in]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetCanonicalFormatEtc )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatectIn,					// [unique][in]
		FORMATETC __RPC_FAR *pformatetcOut					// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *SetData )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatetc,					// [unique][in]
		STGMEDIUM __RPC_FAR *pmedium,						// [unique][in]
		BOOL fRelease
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *EnumFormatEtc )(
		WB_IDataObject __RPC_FAR * This,
		DWORD dwDirection,
		IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenumFormatEtc// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DAdvise )(
		WB_IDataObject __RPC_FAR * This,
		FORMATETC __RPC_FAR *pformatetc,
		DWORD advf,
		IAdviseSink __RPC_FAR *pAdvSink,					// [unique][in]
		DWORD __RPC_FAR *pdwConnection						// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DUnadvise )(
		WB_IDataObject __RPC_FAR * This,
		DWORD dwConnection
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *EnumDAdvise )(
		WB_IDataObject __RPC_FAR * This,
		IEnumSTATDATA __RPC_FAR *__RPC_FAR *ppenumAdvise	// [out]
	);

	END_INTERFACE
} WB_IDataObjectVtbl;

typedef struct WB_IEnumFORMATETCVtbl
{
	BEGIN_INTERFACE

	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )(
		WB_IEnumFORMATETC __RPC_FAR * This,
		REFIID riid,
		void __RPC_FAR *__RPC_FAR *ppvObject				// [iid_is][out]
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )(
		WB_IEnumFORMATETC __RPC_FAR * This
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )(
		WB_IEnumFORMATETC __RPC_FAR * This
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Next )(
		WB_IEnumFORMATETC __RPC_FAR * This,
		ULONG celt,
		FORMATETC __RPC_FAR *rgelt,				// [length_is][size_is][out]
		ULONG __RPC_FAR *pceltFetched								// [out]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Skip )(
		WB_IEnumFORMATETC __RPC_FAR * This,
		ULONG celt
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Reset )(
		WB_IEnumFORMATETC __RPC_FAR * This
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Clone )(
		WB_IEnumFORMATETC __RPC_FAR * This,
		WB_IEnumFORMATETC __RPC_FAR *__RPC_FAR *ppenum		// [out]
	);

	END_INTERFACE
} WB_IEnumFORMATETCVtbl;

typedef struct WB_IDropSourceVtbl
{
	BEGIN_INTERFACE

	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )(
	WB_IDropSource __RPC_FAR * This,
		REFIID riid,
		void __RPC_FAR *__RPC_FAR *ppvObject				// [iid_is][out]
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )(
		WB_IDropSource __RPC_FAR * This
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )(
		WB_IDropSource __RPC_FAR * This
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryContinueDrag )(
		WB_IDropSource __RPC_FAR * This,
		BOOL fEscapePressed,
		DWORD grfKeyState
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GiveFeedback )(
		WB_IDropSource __RPC_FAR * This,
		DWORD dwEffect
	);

	END_INTERFACE
} WB_IDropSourceVtbl;

typedef struct WB_IDropTargetVtbl
{
	BEGIN_INTERFACE

	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )(
		WB_IDropTarget __RPC_FAR * This,
		REFIID riid,
		void __RPC_FAR *__RPC_FAR *ppvObject				// [iid_is][out]
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )(
		WB_IDropTarget __RPC_FAR * This
	);
	ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )(
		WB_IDropTarget __RPC_FAR * This
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragEnter )(
		WB_IDropTarget __RPC_FAR * This,
		WB_IDataObject __RPC_FAR *pDataObj,					// [unique][in]
		DWORD grfKeyState,
		POINTL pt,
		DWORD __RPC_FAR *pdwEffect							// [out][in]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragOver )(
		WB_IDropTarget __RPC_FAR * This,
		DWORD grfKeyState,
		POINTL pt,
		DWORD __RPC_FAR *pdwEffect							// [out][in]
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *DragLeave )(
		WB_IDropTarget __RPC_FAR * This
	);
	HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Drop )(
		WB_IDropTarget __RPC_FAR * This,
		IDataObject __RPC_FAR *pDataObj,					// [unique][in]
		DWORD grfKeyState,
		POINTL pt,
		DWORD __RPC_FAR *pdwEffect							// [out][in]
	);

	END_INTERFACE
} WB_IDropTargetVtbl;

typedef WB_IDataObject* LPWBDATAOBJECT;
typedef WB_IEnumFORMATETC* LPWBFORMATETC;
WB_IEnumFORMATETC *WB_IEnumFORMATETC_new (UINT, FORMATETC *);

//
//	DropEffect private helper routine
//
static DWORD DropEffect(DWORD grfKeyState, POINTL pt, DWORD dwAllowed)
{
	DWORD dwEffect = 0;
	if(dwAllowed & DROPEFFECT_MOVE) dwEffect = DROPEFFECT_MOVE;
	if(dwAllowed & DROPEFFECT_COPY) dwEffect = DROPEFFECT_COPY;
	if ( grfKeyState & MK_SHIFT )   dwEffect = dwAllowed & DROPEFFECT_MOVE;
	if ( grfKeyState & MK_CONTROL ) dwEffect = dwAllowed & DROPEFFECT_COPY;

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
									REFIID riid,
								LPVOID *ppvObject)
{
	*ppvObject = NULL;
	if ( IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget) ) 
	{
		idroptarget_addref (This);
		*ppvObject = This;
		return S_OK;
	}
	else
		return E_NOINTERFACE;
}
//
//	IUnknown::Release
//
static ULONG STDMETHODCALLTYPE
idroptarget_release (WB_IDropTarget* This)
{
	LONG count = InterlockedDecrement(&This->m_lRefCount);

	if(count == 0) {
		LocalFree(This);
		return 0;
	}
	else
		return count;
}
//
//	IDropTarget::DragEnter
//
//
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragenter( WB_IDropTarget *This,
												WB_IDataObject *pDataObject, 
												DWORD grfKeyState,
												POINTL pt, DWORD *pdwEffect)
{
	// does the dataobject contain data we want?
	FORMATETC fmtetc = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	// does the data object support CF_TEXT using a HGLOBAL?
	HRESULT rc=pDataObject->ido.lpVtbl->QueryGetData((LPDATAOBJECT)pDataObject,
																	&fmtetc);
	This->m_fAllowDrop = rc==S_OK ? TRUE : FALSE;

	if(This->m_fAllowDrop) {
		// get the dropeffect based on keyboard state
		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);
		SetFocus(This->m_hWnd);
	}
	else {
		*pdwEffect = DROPEFFECT_NONE;
	}
	return S_OK;
}

//
//	IDropTarget::DragOver
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragover( WB_IDropTarget* This, 
														DWORD grfKeyState,
														POINTL pt,
														DWORD *pdwEffect )
{
	if(This->m_fAllowDrop) {
		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);
	}
	else {
		*pdwEffect = DROPEFFECT_NONE;
	}
	return S_OK;
}

//
//	IDropTarget::DragLeave
//
static HRESULT STDMETHODCALLTYPE idroptarget_dragleave( WB_IDropTarget* This )
{
	return S_OK;
}

//
//	IDropTarget::Drop
//
void ( *dropHandler )(char *);
static HRESULT STDMETHODCALLTYPE idroptarget_drop( WB_IDropTarget* This,
													IDataObject * pDataObject,
													DWORD grfKeyState,
													POINTL pt, DWORD *pdwEffect)
{
	*pdwEffect = DROPEFFECT_NONE;

	if(This->m_fAllowDrop) {
		*pdwEffect = DropEffect(grfKeyState, pt, *pdwEffect);
		FORMATETC fmtetc = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		// See if the dataobject contains any TEXT stored as a HGLOBAL
		if(pDataObject->lpVtbl->QueryGetData(pDataObject, &fmtetc) == S_OK)	{
			STGMEDIUM stgmed;
			if(pDataObject->lpVtbl->GetData(pDataObject,&fmtetc,&stgmed)==S_OK)
			{
				PVOID data = GlobalLock(stgmed.hGlobal);//lock to access HGLOBAL
				dropHandler(strdup((char *)data));
				GlobalUnlock(stgmed.hGlobal);
				ReleaseStgMedium(&stgmed);
			}
		}
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

//
//	Constructor for the CDropTarget class
//
WB_IDropTarget * WB_IDropTarget_new(HWND hwnd)
{
	WB_IDropTarget *result;

	result = (WB_IDropTarget *)LocalAlloc(LPTR, sizeof(WB_IDropTarget));
	result->idt.lpVtbl = (IDropTargetVtbl*)&idt_vtbl;
	result->m_lRefCount = 1;
	result->m_hWnd = hwnd;
	result->m_fAllowDrop = FALSE;

	return result;
}
HRESULT CreateDropTarget(HWND hwnd, WB_IDropTarget **ppDropTarget)
{
	if(ppDropTarget == 0) return E_INVALIDARG;
	*ppDropTarget = WB_IDropTarget_new(hwnd);
	return (*ppDropTarget) ? S_OK : E_OUTOFMEMORY;
}

void RegisterDropWindow(HWND hwnd, WB_IDropTarget **ppDropTarget)
{
	WB_IDropTarget *pDropTarget;
	CreateDropTarget(hwnd, &pDropTarget);		// acquire a strong lock
	CoLockObjectExternal((struct IUnknown*)pDropTarget, TRUE, FALSE);
	RegisterDragDrop(hwnd, (LPDROPTARGET)pDropTarget);
	*ppDropTarget = pDropTarget;
}

void UnregisterDropWindow(HWND hwnd, IDropTarget *pDropTarget)
{
	RevokeDragDrop(hwnd);						// remove the strong lock
	CoLockObjectExternal((struct IUnknown*)pDropTarget, FALSE, TRUE);
	pDropTarget->lpVtbl->Release(pDropTarget);	// release our own reference
}

static IDropTarget *pDropTarget;
void drop_Init( HWND hwnd, void (*handler)(char*) )
{
	dropHandler = handler;
	RegisterDropWindow(hwnd, (WB_IDropTarget **)&pDropTarget);
}
void drop_Destroy( HWND hwnd )
{
	UnregisterDropWindow(hwnd, pDropTarget);
}