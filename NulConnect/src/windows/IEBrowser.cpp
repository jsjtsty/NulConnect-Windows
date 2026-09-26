#include "pch.h"
#include "windows/IEBrowser.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

#include <exdispid.h>
#include <urlmon.h>

#pragma comment(lib, "urlmon.lib")

// Hidden by the SDK headers for a Windows 7 target; supported by WinINet on
// Internet Explorer 8 and later.
#ifndef INTERNET_SUPPRESS_COOKIE_PERSIST
#define INTERNET_SUPPRESS_COOKIE_PERSIST 3
#endif

namespace nc {

namespace {

// Registry-only switch: without it the control renders in IE7 document mode.
void EnableIe11DocumentMode() {
    std::wstring exe = ExecutablePath();
    std::wstring name = exe.substr(exe.find_last_of(L"\\/") + 1);
    HKEY key = nullptr;
    constexpr const wchar_t* path =
        L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        DWORD mode = 11001;  // IE11 edge mode, regardless of !DOCTYPE
        RegSetValueExW(key, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&mode), sizeof(mode));
        RegCloseKey(key);
    }
}

std::wstring VariantUrl(const VARIANT& value) {
    const VARIANT* v = &value;
    if (v->vt == (VT_BYREF | VT_VARIANT) && v->pvarVal) v = v->pvarVal;
    if (v->vt == VT_BSTR && v->bstrVal) return std::wstring(v->bstrVal, SysStringLen(v->bstrVal));
    return {};
}

void SetCancel(VARIANT& value) {
    if (value.vt == (VT_BYREF | VT_BOOL) && value.pboolVal) *value.pboolVal = VARIANT_TRUE;
}

}  // namespace

void IEBrowser::ConfigureEngine() {
    static bool configured = false;
    if (configured) return;
    configured = true;
    EnableIe11DocumentMode();
    // Per-process feature switches (no registry changes).
    CoInternetSetFeatureEnabled(FEATURE_DISABLE_NAVIGATION_SOUNDS, SET_FEATURE_ON_PROCESS, TRUE);
    CoInternetSetFeatureEnabled(FEATURE_WEBOC_POPUPMANAGEMENT, SET_FEATURE_ON_PROCESS, TRUE);
    // Keep the sign-in cookies in memory only: they are not needed after the
    // callback is captured, and must not linger in the shared IE store.
    DWORD suppress = INTERNET_SUPPRESS_COOKIE_PERSIST;
    InternetSetOptionW(nullptr, INTERNET_OPTION_SUPPRESS_BEHAVIOR, &suppress, sizeof(suppress));
}

void IEBrowser::EndBrowserSession() {
    InternetSetOptionW(nullptr, INTERNET_OPTION_END_BROWSER_SESSION, nullptr, 0);
}

Microsoft::WRL::ComPtr<IEBrowser> IEBrowser::Create(HWND parent, const RECT& bounds, Callbacks callbacks) {
    ConfigureEngine();
    Microsoft::WRL::ComPtr<IEBrowser> browser;
    browser.Attach(new IEBrowser(parent, bounds, std::move(callbacks)));
    if (!browser->Initialize()) {
        browser->Close();
        return nullptr;
    }
    return browser;
}

IEBrowser::IEBrowser(HWND parent, const RECT& bounds, Callbacks callbacks)
    : parent_(parent), bounds_(bounds), callbacks_(std::move(callbacks)) {}

IEBrowser::~IEBrowser() = default;

bool IEBrowser::Initialize() {
    if (FAILED(CoCreateInstance(CLSID_WebBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&oleObject_)))) {
        Log("[WebLogin] WebBrowser control unavailable");
        return false;
    }
    if (FAILED(oleObject_->SetClientSite(static_cast<IOleClientSite*>(this)))) return false;
    OleSetContainedObject(oleObject_.Get(), TRUE);
    RECT rect = bounds_;
    if (FAILED(oleObject_->DoVerb(OLEIVERB_INPLACEACTIVATE, nullptr, static_cast<IOleClientSite*>(this), 0, parent_, &rect))) {
        return false;
    }
    if (FAILED(oleObject_.As(&browser_))) return false;
    browser_->put_Silent(VARIANT_TRUE);  // no script error dialogs
    browser_->put_RegisterAsDropTarget(VARIANT_FALSE);
    Microsoft::WRL::ComPtr<IConnectionPointContainer> container;
    Microsoft::WRL::ComPtr<IConnectionPoint> point;
    if (SUCCEEDED(browser_.As(&container)) && SUCCEEDED(container->FindConnectionPoint(DIID_DWebBrowserEvents2, &point))) {
        point->Advise(static_cast<IDispatch*>(this), &adviseCookie_);
    }
    Log("[WebLogin] using the Internet Explorer engine");
    return adviseCookie_ != 0;
}

void IEBrowser::Navigate(const std::wstring& url) {
    if (!browser_) return;
    VARIANT target{};
    target.vt = VT_BSTR;
    target.bstrVal = SysAllocString(url.c_str());
    VARIANT empty{};
    browser_->Navigate2(&target, &empty, &empty, &empty, &empty);
    VariantClear(&target);
}

void IEBrowser::Stop() {
    if (browser_) browser_->Stop();
}

void IEBrowser::SetBounds(const RECT& bounds) {
    bounds_ = bounds;
    Microsoft::WRL::ComPtr<IOleInPlaceObject> inPlace;
    if (oleObject_ && SUCCEEDED(oleObject_.As(&inPlace))) inPlace->SetObjectRects(&bounds_, &bounds_);
}

void IEBrowser::Close() {
    if (closed_) return;
    closed_ = true;
    if (browser_ && adviseCookie_) {
        Microsoft::WRL::ComPtr<IConnectionPointContainer> container;
        Microsoft::WRL::ComPtr<IConnectionPoint> point;
        if (SUCCEEDED(browser_.As(&container)) && SUCCEEDED(container->FindConnectionPoint(DIID_DWebBrowserEvents2, &point))) {
            point->Unadvise(adviseCookie_);
        }
        adviseCookie_ = 0;
    }
    if (browser_) browser_->Stop();
    if (oleObject_) {
        Microsoft::WRL::ComPtr<IOleInPlaceObject> inPlace;
        if (SUCCEEDED(oleObject_.As(&inPlace))) inPlace->InPlaceDeactivate();
        oleObject_->Close(OLECLOSE_NOSAVE);
        // Breaks the control -> site reference cycle.
        oleObject_->SetClientSite(nullptr);
    }
    browser_.Reset();
    oleObject_.Reset();
    callbacks_ = {};
}

bool IEBrowser::PreTranslate(MSG& message) {
    if (!browser_ || message.message < WM_KEYFIRST || message.message > WM_KEYLAST) return false;
    Microsoft::WRL::ComPtr<IOleInPlaceObject> inPlace;
    HWND control = nullptr;
    if (FAILED(oleObject_.As(&inPlace)) || FAILED(inPlace->GetWindow(&control)) || !control) return false;
    if (message.hwnd != control && !IsChild(control, message.hwnd)) return false;
    Microsoft::WRL::ComPtr<IOleInPlaceActiveObject> active;
    if (FAILED(browser_.As(&active))) return false;
    return active->TranslateAccelerator(&message) == S_OK;
}

bool IEBrowser::IsTopLevel(IDispatch* frame) const {
    if (!frame || !browser_) return false;
    Microsoft::WRL::ComPtr<IUnknown> left, right;
    frame->QueryInterface(IID_PPV_ARGS(&left));
    browser_->QueryInterface(IID_PPV_ARGS(&right));
    return left && left == right;
}

void IEBrowser::Capture(const std::wstring& url) {
    if (captured_) return;
    captured_ = true;
    if (browser_) browser_->Stop();
    if (callbacks_.onCapture) callbacks_.onCapture(url);
}

STDMETHODIMP IEBrowser::Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT*, EXCEPINFO*, UINT*) {
    if (!params || closed_) return S_OK;
    // DISPPARAMS arguments are in reverse order.
    VARIANT* args = params->rgvarg;
    UINT count = params->cArgs;
    auto shouldCapture = [this](const std::wstring& url) {
        return !url.empty() && callbacks_.shouldCapture && callbacks_.shouldCapture(url);
    };
    switch (id) {
    case DISPID_BEFORENAVIGATE2:
        // (pDisp, URL, Flags, TargetFrameName, PostData, Headers, Cancel)
        if (count == 7) {
            std::wstring url = VariantUrl(args[5]);
            if (captured_ || shouldCapture(url)) {
                SetCancel(args[0]);
                Capture(url);
            } else {
                Log("[WebLogin] IE navigate " + LoggableUrl(Narrow(url)));
            }
        }
        break;
    case DISPID_NAVIGATECOMPLETE2:
        // (pDisp, URL). Server-side redirects do not raise BeforeNavigate2
        // in this control; catch a redirected callback here instead, as the
        // macOS client does from the navigation response.
        if (count == 2 && shouldCapture(VariantUrl(args[0]))) Capture(VariantUrl(args[0]));
        break;
    case DISPID_NAVIGATEERROR:
        // (pDisp, URL, TargetFrameName, StatusCode, Cancel)
        if (count == 5) {
            std::wstring url = VariantUrl(args[3]);
            if (shouldCapture(url)) {
                SetCancel(args[0]);
                Capture(url);
                break;
            }
            long status = 0;
            const VARIANT* code = &args[1];
            if (code->vt == (VT_BYREF | VT_VARIANT) && code->pvarVal) code = code->pvarVal;
            if (code->vt == VT_I4) status = code->lVal;
            bool topLevel = args[4].vt == VT_DISPATCH && IsTopLevel(args[4].pdispVal);
            Log("[WebLogin] IE navigation error status=" + std::to_string(status));
            // Negative values are INET_E_* transport failures; positive ones
            // are HTTP statuses, which still render a page.
            if (topLevel && callbacks_.onCompleted) callbacks_.onCompleted(status < 0, status);
        }
        break;
    case DISPID_DOCUMENTCOMPLETE:
        // (pDisp, URL)
        if (count == 2 && !captured_ && args[1].vt == VT_DISPATCH && IsTopLevel(args[1].pdispVal)) {
            if (callbacks_.onCompleted) callbacks_.onCompleted(false, 0);
        }
        break;
    case DISPID_NEWWINDOW3:
        // (ppDisp, Cancel, dwFlags, bstrUrlContext, bstrUrl): keep pop-ups
        // in this window.
        if (count == 5) {
            SetCancel(args[3]);
            if (args[0].vt == VT_BSTR && args[0].bstrVal) {
                std::wstring url(args[0].bstrVal, SysStringLen(args[0].bstrVal));
                if (shouldCapture(url)) Capture(url);
                else Navigate(url);
            }
        }
        break;
    default:
        break;
    }
    return S_OK;
}

STDMETHODIMP IEBrowser::GetHostInfo(DOCHOSTUIINFO* info) {
    if (!info) return E_POINTER;
    info->cbSize = sizeof(DOCHOSTUIINFO);
    info->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_DPI_AWARE | DOCHOSTUIFLAG_THEME |
                    DOCHOSTUIFLAG_ENABLE_REDIRECT_NOTIFICATION;
    info->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
    info->pchHostCss = nullptr;
    info->pchHostNS = nullptr;
    return S_OK;
}

STDMETHODIMP IEBrowser::GetContainer(IOleContainer** container) {
    if (container) *container = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP IEBrowser::GetWindow(HWND* window) {
    if (!window) return E_POINTER;
    *window = parent_;
    return S_OK;
}

STDMETHODIMP IEBrowser::GetWindowContext(IOleInPlaceFrame** frame, IOleInPlaceUIWindow** document, LPRECT position,
                                         LPRECT clip, LPOLEINPLACEFRAMEINFO info) {
    if (frame) {
        *frame = static_cast<IOleInPlaceFrame*>(this);
        AddRef();
    }
    if (document) *document = nullptr;
    if (position) *position = bounds_;
    if (clip) *clip = bounds_;
    if (info) {
        info->fMDIApp = FALSE;
        info->hwndFrame = parent_;
        info->haccel = nullptr;
        info->cAccelEntries = 0;
    }
    return S_OK;
}

STDMETHODIMP IEBrowser::QueryInterface(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (riid == IID_IUnknown || riid == IID_IOleClientSite) *object = static_cast<IOleClientSite*>(this);
    else if (riid == IID_IOleWindow || riid == IID_IOleInPlaceSite) *object = static_cast<IOleInPlaceSite*>(this);
    else if (riid == IID_IOleInPlaceUIWindow || riid == IID_IOleInPlaceFrame) *object = static_cast<IOleInPlaceFrame*>(this);
    else if (riid == IID_IDocHostUIHandler) *object = static_cast<IDocHostUIHandler*>(this);
    else if (riid == IID_IDispatch || riid == DIID_DWebBrowserEvents2) *object = static_cast<IDispatch*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) IEBrowser::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&refs_));
}

STDMETHODIMP_(ULONG) IEBrowser::Release() {
    LONG refs = InterlockedDecrement(&refs_);
    if (refs == 0) delete this;
    return static_cast<ULONG>(refs);
}

}  // namespace nc
