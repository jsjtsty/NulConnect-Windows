#pragma once

#include <exdisp.h>
#include <mshtmhst.h>
#include <oleidl.h>

#include <functional>
#include <string>

namespace nc {

// Hosts the system WebBrowser control (Internet Explorer 11 engine) as a
// fallback when the WebView2 Runtime is unavailable, e.g. on Windows 7 or on
// Windows 10 installations without Edge. Implements the minimal OLE
// container interfaces plus DWebBrowserEvents2 for navigation interception.
class IEBrowser final : public IOleClientSite,
                        public IOleInPlaceSite,
                        public IOleInPlaceFrame,
                        public IDocHostUIHandler,
                        public IDispatch {
public:
    struct Callbacks {
        // True when `url` is the SSO callback that must be captured.
        std::function<bool(const std::wstring&)> shouldCapture;
        std::function<void(const std::wstring&)> onCapture;
        // Top-level navigation finished. `networkFailure` is true only when
        // no usable page could be loaded (not for HTTP error statuses).
        std::function<void(bool networkFailure, long status)> onCompleted;
    };

    // Returns nullptr when the control cannot be created.
    static Microsoft::WRL::ComPtr<IEBrowser> Create(HWND parent, const RECT& bounds, Callbacks callbacks);

    void Navigate(const std::wstring& url);
    void Stop();
    void SetBounds(const RECT& bounds);
    void Close();
    // Gives the control a chance to handle keyboard accelerators (Tab,
    // Enter, Backspace in forms). Returns true when the message was consumed.
    bool PreTranslate(MSG& message);

    // Session cookies of the embedded engine are kept in memory only; this
    // ends the WinINet browser session (sign-out).
    static void EndBrowserSession();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IOleClientSite
    STDMETHODIMP SaveObject() override { return E_NOTIMPL; }
    STDMETHODIMP GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
    STDMETHODIMP GetContainer(IOleContainer** container) override;
    STDMETHODIMP ShowObject() override { return S_OK; }
    STDMETHODIMP OnShowWindow(BOOL) override { return S_OK; }
    STDMETHODIMP RequestNewObjectLayout() override { return E_NOTIMPL; }

    // IOleWindow (shared by IOleInPlaceSite and IOleInPlaceFrame)
    STDMETHODIMP GetWindow(HWND* window) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // IOleInPlaceSite
    STDMETHODIMP CanInPlaceActivate() override { return S_OK; }
    STDMETHODIMP OnInPlaceActivate() override { return S_OK; }
    STDMETHODIMP OnUIActivate() override { return S_OK; }
    STDMETHODIMP GetWindowContext(IOleInPlaceFrame** frame, IOleInPlaceUIWindow** document, LPRECT position, LPRECT clip,
                                  LPOLEINPLACEFRAMEINFO info) override;
    STDMETHODIMP Scroll(SIZE) override { return E_NOTIMPL; }
    STDMETHODIMP OnUIDeactivate(BOOL) override { return S_OK; }
    STDMETHODIMP OnInPlaceDeactivate() override { return S_OK; }
    STDMETHODIMP DiscardUndoState() override { return E_NOTIMPL; }
    STDMETHODIMP DeactivateAndUndo() override { return E_NOTIMPL; }
    STDMETHODIMP OnPosRectChange(LPCRECT) override { return S_OK; }

    // IOleInPlaceUIWindow / IOleInPlaceFrame
    STDMETHODIMP GetBorder(LPRECT) override { return INPLACE_E_NOTOOLSPACE; }
    STDMETHODIMP RequestBorderSpace(LPCBORDERWIDTHS) override { return INPLACE_E_NOTOOLSPACE; }
    STDMETHODIMP SetBorderSpace(LPCBORDERWIDTHS) override { return S_OK; }
    STDMETHODIMP SetActiveObject(IOleInPlaceActiveObject*, LPCOLESTR) override { return S_OK; }
    STDMETHODIMP InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    STDMETHODIMP SetMenu(HMENU, HOLEMENU, HWND) override { return S_OK; }
    STDMETHODIMP RemoveMenus(HMENU) override { return E_NOTIMPL; }
    STDMETHODIMP SetStatusText(LPCOLESTR) override { return S_OK; }
    STDMETHODIMP EnableModeless(BOOL) override { return S_OK; }
    STDMETHODIMP TranslateAccelerator(LPMSG, WORD) override { return S_FALSE; }

    // IDocHostUIHandler
    STDMETHODIMP ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) override { return S_FALSE; }
    STDMETHODIMP GetHostInfo(DOCHOSTUIINFO* info) override;
    STDMETHODIMP ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*, IOleInPlaceFrame*,
                        IOleInPlaceUIWindow*) override {
        return S_FALSE;
    }
    STDMETHODIMP HideUI() override { return S_OK; }
    STDMETHODIMP UpdateUI() override { return S_OK; }
    STDMETHODIMP OnDocWindowActivate(BOOL) override { return S_OK; }
    STDMETHODIMP OnFrameWindowActivate(BOOL) override { return S_OK; }
    STDMETHODIMP ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    STDMETHODIMP TranslateAccelerator(LPMSG, const GUID*, DWORD) override { return S_FALSE; }
    STDMETHODIMP GetOptionKeyPath(LPOLESTR* key, DWORD) override {
        if (key) *key = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP GetDropTarget(IDropTarget*, IDropTarget**) override { return E_NOTIMPL; }
    STDMETHODIMP GetExternal(IDispatch** external) override {
        if (external) *external = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP TranslateUrl(DWORD, LPWSTR, LPWSTR* translated) override {
        if (translated) *translated = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP FilterDataObject(IDataObject*, IDataObject** result) override {
        if (result) *result = nullptr;
        return S_FALSE;
    }

    // IDispatch (DWebBrowserEvents2 sink)
    STDMETHODIMP GetTypeInfoCount(UINT* count) override {
        *count = 0;
        return S_OK;
    }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT*, EXCEPINFO*, UINT*) override;

private:
    IEBrowser(HWND parent, const RECT& bounds, Callbacks callbacks);
    ~IEBrowser();
    bool Initialize();
    bool IsTopLevel(IDispatch* frame) const;
    void Capture(const std::wstring& url);
    static void ConfigureEngine();

    LONG refs_ = 1;
    HWND parent_;
    RECT bounds_;
    Callbacks callbacks_;
    Microsoft::WRL::ComPtr<IOleObject> oleObject_;
    Microsoft::WRL::ComPtr<IWebBrowser2> browser_;
    DWORD adviseCookie_ = 0;
    bool captured_ = false;
    bool closed_ = false;
};

}  // namespace nc
