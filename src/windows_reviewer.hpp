#pragma once
#ifdef DRIFTER_NEXUS_REVIEWER
#include <array>
#include <string>
#include <windows.h>

namespace drifter {
// This native dialog is absent from ordinary release builds. No browser field,
// command line, environment variable, or developer credential supplies the key.
struct ReviewerInput {
    std::array<wchar_t, 4097> key{};
    ~ReviewerInput() { SecureZeroMemory(key.data(), sizeof(key)); }
    ReviewerInput() = default;
    ReviewerInput(const ReviewerInput&) = delete;
    ReviewerInput& operator=(const ReviewerInput&) = delete;
};
inline INT_PTR CALLBACK reviewerDialog(HWND dialog, UINT message, WPARAM parameter, LPARAM data) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, data);
        SendDlgItemMessageW(dialog, 1401, EM_SETLIMITTEXT, 4096, 0);
        SetFocus(GetDlgItem(dialog, 1401));
        return FALSE;
    }
    if (message == WM_CLOSE) {
        SetDlgItemTextW(dialog, 1401, L"");
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(parameter) == IDOK) {
            auto* input = reinterpret_cast<ReviewerInput*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (input) GetDlgItemTextW(dialog, 1401, input->key.data(), static_cast<int>(input->key.size()));
            SetDlgItemTextW(dialog, 1401, L"");
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(parameter) == IDCANCEL) {
            SetDlgItemTextW(dialog, 1401, L"");
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
inline bool promptReviewerKey(HWND owner, ReviewerInput& input) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1400), owner, reviewerDialog,
                           reinterpret_cast<LPARAM>(&input)) == IDOK;
}
}
#endif
