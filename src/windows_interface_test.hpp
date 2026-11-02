#pragma once
#ifdef DRIFTER_UI_TESTING
json interfaceTestLastState;
// Included in the native shell's private namespace after its COM callback helper.
// No test entrypoint is present in ordinary or reviewer release builds.
bool validInterfaceTestOutput(const fs::path& path) {
    wchar_t temp[32768]{};
    if (!GetTempPathW(32768, temp)) return false;
    std::error_code issue;
    auto root = fs::weakly_canonical(fs::path(temp), issue);
    if (issue || !path.is_absolute() || fs::exists(path)) return false;
    auto parent = fs::canonical(path.parent_path(), issue);
    if (issue || parent.parent_path() != root || parent.filename().wstring().rfind(L"DrifterInterfaceTest-", 0) != 0)
        return false;
    return path.filename() == L"interface.json";
}
void finishInterfaceTest(json result) {
    if (interfaceTestOutput.empty()) return;
    KillTimer(window, InterfaceTestTimer);
    KillTimer(window, StartupTimer);
    // Exercise the actual native admission path without a file mutation. A
    // file picker can return after a nested message loop replaces its renderer.
    const auto stoppedGeneration = rendererEpoch.current();
    bool stalePickerRejected = false, nestedScopeRestored = false, freshScopeCurrent = false, nativeWorkAdmitted = false;
    {
        RendererRequestScope oldPicker(stoppedGeneration);
        rendererEpoch.advance();
        {
            RendererRequestScope freshNativeEvent;
            freshScopeCurrent = rendererEpoch.current(requestRendererEpoch);
            nativeWorkAdmitted = enqueue({{"action", "snapshot"}, {"requestId", "private-native-monitor"}});
        }
        nestedScopeRestored = requestRendererEpoch == stoppedGeneration;
        stalePickerRejected = !enqueue({{"action", "snapshot"}, {"requestId", "private-stale-picker"}});
    }
    result["rendererEpochGuards"] = {{"stalePickerRejected", stalePickerRejected},
        {"nestedScopeRestored", nestedScopeRestored}, {"freshScopeCurrent", freshScopeCurrent},
        {"nativeWorkAdmitted", nativeWorkAdmitted},
        {"oldReplyRejected", !rendererEpoch.current(stoppedGeneration)}};
    if (!stalePickerRejected || !nestedScopeRestored || !freshScopeCurrent || !nativeWorkAdmitted) result["ready"] = false;
    result["nativeMetrics"] = drifter::windowsDisplayMetrics(window);
    result["windowWasVisible"] = IsWindowVisible(window) != FALSE;
    result["navigationCompleted"] = initialWindowShown;
    result["attempts"] = interfaceTestAttempts;
    result["localEdition"] = drifter::LocalEdition;
    result["keyCheck"] = drifter::LocalEdition ? "local-edition-denial" : "native-key-validation";
    bool duplicateRejected = false;
    try {
        drifter::parseBoundedJson(R"({"requestId":"private-duplicate","action":"snapshot","action":"scan"})",
                                  128 * 1024, 8192);
    } catch (...) {
        duplicateRejected = true;
    }
    result["bridgeDuplicateRejected"] = duplicateRejected;
    if (!duplicateRejected) result["ready"] = false;
    result["profileFiles"] = drifter::profileInterfaceFiles(stateDir);
    if (!result["profileFiles"].value("ready", false)) result["ready"] = false;
    auto body = result.dump(2);
    auto file =
        CreateFileW(interfaceTestOutput.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    const bool good = file != INVALID_HANDLE_VALUE && body.size() < 1024 * 1024 &&
                      WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr) &&
                      written == body.size();
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    interfaceTestExitCode = good && result.value("ready", false) ? 0 : 8;
    interfaceTestOutput.clear();
    PostMessageW(window, WM_CLOSE, 0, 0);
}
void pollInterfaceTest() {
    if (interfaceTestBusy || interfaceTestOutput.empty() || !web) return;
    if (++interfaceTestAttempts > 150) {
        auto result = interfaceTestLastState.is_object() ? interfaceTestLastState : json::object();
        result["ready"] = false;
        result["error"] = "The sandbox page did not become ready before the bounded test deadline.";
        finishInterfaceTest(std::move(result));
        return;
    }
    interfaceTestBusy = true;
    const auto script = L"window.__toolkitExpectedKeyError=" +
        wide(json(drifter::LocalEdition ? drifter::LocalEditionMessage : "Paste your own Nexus API key").dump()) +
        LR"(;(()=>{
 if(window.chrome?.webview&&!window.__nativeBridgeProbe){
  const probe=window.__nativeBridgeProbe={alive:false,rejectedReplies:0,internalRefused:false,keyRouted:false,keyEchoed:false};
  window.chrome.webview.addEventListener('message',event=>{const r=event.data;if(r?.requestId==='private-bounded-depth'||r?.requestId==='private-bounded-events')probe.rejectedReplies++;if(r?.requestId==='private-bounded-valid')probe.alive=r.ok===true&&Array.isArray(r.data?.mods);if(r?.requestId==='private-bounded-internal')probe.internalRefused=r.ok===false&&String(r.error).includes('native operation requires its reviewed workflow');if(r?.requestId==='private-bounded-key'){probe.keyRouted=r.ok===false&&String(r.error).includes(window.__toolkitExpectedKeyError);probe.keyEchoed=JSON.stringify(r).includes('private probe key');}});
  // The native LocalEdition gate or API validator must refuse this synthetic
  // malformed key offline. Neither mode may echo the input or send traffic.
  window.chrome.webview.postMessage({requestId:'private-bounded-key',action:'nexusConnect',apiKey:'private probe key'});
  const deep={};let node=deep;for(let i=0;i<80;i++){node.next={};node=node.next;}
  window.chrome.webview.postMessage({requestId:'private-bounded-depth',action:'snapshot',payload:deep});
  window.chrome.webview.postMessage({requestId:'private-bounded-events',action:'snapshot',payload:Array(8300).fill(0)});
  window.chrome.webview.postMessage({requestId:'private-bounded-internal',action:'profilePrepareLaunch',name:'private-no-profile'});
  window.chrome.webview.postMessage({requestId:'private-bounded-valid',action:'snapshot'});
 }
 const test=window.__toolkitSmoke,main=document.querySelector('#main-content'),ready=!!test?.ready(),box=main?.getBoundingClientRect(),seasonal=test?.seasonal(),bridge=window.__nativeBridgeProbe;
 if(ready&&bridge?.alive)test.profileWorkflowStart();const profiles=test?.profileWorkflowStatus();
 if(profiles?.passed===true)test.changelogWorkflowStart();const changelog=test?.changelogWorkflowStatus();
 return {ready:ready&&!!box&&box.width>0&&main.innerText.length>80&&seasonal?.artwork==='local-svg-sprite'&&!!bridge?.alive&&bridge.rejectedReplies===0&&bridge.internalRefused&&bridge.keyRouted&&!bridge.keyEchoed&&profiles?.passed===true&&changelog?.passed===true,mainCharacters:main?.innerText.length||0,navigationItems:document.querySelectorAll('.sidebar [data-view]').length,scaling:test?.scaling(),seasonal,bridgeChecks:bridge,profileWorkflow:profiles,changelogWorkflow:changelog,errorVisible:document.querySelector('#interface-recovery')?.hidden===false};
 })())";
    auto response = callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
        [](HRESULT status, LPCWSTR raw) -> HRESULT {
            interfaceTestBusy = false;
            if (interfaceTestOutput.empty()) return S_OK;
            if (SUCCEEDED(status) && raw) {
                try {
                    auto result = json::parse(utf8(raw));
                    if (result.is_object()) interfaceTestLastState = result;
                    if (result.is_object() && result.value("ready", false)) {
                        finishInterfaceTest(std::move(result));
                    }
                } catch (...) {}
            }
            return S_OK;
        });
    if (FAILED(web->ExecuteScript(script.c_str(), response))) interfaceTestBusy = false;
    response->Release();
}
#endif
