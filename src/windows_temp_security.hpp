#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <string>

namespace drifter::windows_temp_detail {
inline std::wstring privateWorkspacePolicy(const std::wstring& userSid) {
    // A protected DACL does not select an owner. An elevated token can otherwise
    // choose Administrators, which is different from its creating user's SID.
    return L"O:" + userSid + L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + userSid + L")";
}
inline bool cleanupOwnerAllowed(PSID owner, PSID user, PSID creatorOwner, PACL acl) {
    if (!owner || !user || !IsValidSid(owner) || !IsValidSid(user)) return false;
    if (EqualSid(owner, user)) return true;
    // Ordinary files created inside the private root still inherit the token's
    // default owner. Admit that exact captured identity only during cleanup and
    // only with a private allow-list ACL; root/work-directory admission stays
    // strictly current-user-owned. Never admit arbitrary administrator owners.
    if (!creatorOwner || !IsValidSid(creatorOwner) || !EqualSid(owner, creatorOwner) || !acl || !IsValidAcl(acl) ||
        !acl->AceCount || acl->AceCount > 16)
        return false;
    std::array<DWORD, (SECURITY_MAX_SID_SIZE + sizeof(DWORD) - 1) / sizeof(DWORD)> system{};
    DWORD systemBytes = sizeof(system);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &systemBytes)) return false;
    bool userAccess = false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(acl, i, &raw) || !raw) return false;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
            ace->Header.AceSize < offsetof(ACCESS_ALLOWED_ACE, SidStart) + GetSidLengthRequired(0))
            return false;
        auto granted = reinterpret_cast<PSID>(&ace->SidStart);
        if (!IsValidSid(granted) ||
            GetLengthSid(granted) > ace->Header.AceSize - offsetof(ACCESS_ALLOWED_ACE, SidStart))
            return false;
        const bool currentUser = EqualSid(granted, user);
        if (!currentUser && !EqualSid(granted, system.data())) return false;
        if (currentUser && !(ace->Header.AceFlags & INHERIT_ONLY_ACE) && ace->Mask) userAccess = true;
    }
    return userAccess;
}
}
#endif
