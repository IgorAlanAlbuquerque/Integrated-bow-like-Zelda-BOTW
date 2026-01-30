#include "BowState.h"

#include "patchs/SkipEquipController.h"

namespace {
    constexpr std::uint64_t kSkipReturnFallbackDisableMs = 1000;
    constexpr std::uint64_t kSkipReturnDisableAfterMs = 200;

    struct ScopedSkipEquipReturn {
        bool enabled{false};

        ScopedSkipEquipReturn(bool en, RE::PlayerCharacter* pc) : enabled(en) {
            if (enabled) {
                IntegratedBow::SkipEquipController::EnableAndArmDisable(pc, 0, false, kSkipReturnFallbackDisableMs);
            }
        }

        ~ScopedSkipEquipReturn() {
            if (enabled) {
                IntegratedBow::SkipEquipController::ArmDisable(kSkipReturnDisableAfterMs);
            }
        }
    };

    RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject* base, RE::ExtraDataList* candidate) {
        if (!base || !candidate) {
            return nullptr;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return nullptr;
        }

        auto inventory = player->GetInventory();

        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) {
                return nullptr;
            }

            for (auto* x : *entry->extraLists) {
                if (x == candidate) {
                    return x;
                }
            }

            return nullptr;
        }

        return nullptr;
    }

    void RemoveChosenTagFromAnyTaggedInstance(RE::TESBoundObject* base) {
        if (!base) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        auto inventory = player->GetInventory();
        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) {
                return;
            }

            for (auto* x : *entry->extraLists) {
                if (!x) {
                    continue;
                }
                const char* disp = x->GetDisplayName(base);
                if (!disp) {
                    continue;
                }
                std::size_t len = std::strlen(disp);
                if (len >= kTagLen && std::memcmp(disp + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                    BowState::detail::RemoveChosenTagFromInstance(base, x);
                }
            }
            return;
        }
    }

    bool IsHandReady(RE::PlayerCharacter* player, RE::TESBoundObject* desired, bool leftHand) {
        if (!player) return false;

        auto* curForm = player->GetEquippedObject(leftHand);
        auto const* curBase = curForm ? curForm->As<RE::TESBoundObject>() : nullptr;

        if (!desired) {
            return curBase == nullptr;
        }
        return curBase == desired;
    }
}

bool BowState::detail::IsTemperingTag(std::string_view inside) {
    for (auto q : kQualityTags) {
        if (inside.size() != q.size()) {
            continue;
        }

        bool match = true;
        for (std::size_t i = 0; i < q.size(); ++i) {
            const auto c = static_cast<char>(std::tolower(static_cast<unsigned char>(inside[i])));
            if (c != q[i]) {
                match = false;
                break;
            }
        }

        if (match) {
            return true;
        }
    }

    return false;
}

void BowState::detail::TrimTrailingSpaces(std::string& s) {
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
}

void BowState::detail::RemoveChosenTagInplace(std::string& s) {
    constexpr std::string_view chosenTag{" (chosen)"};

    for (;;) {
        auto pos = s.find(chosenTag);
        if (pos == std::string::npos) {
            break;
        }

        s.erase(pos, chosenTag.size());
    }

    TrimTrailingSpaces(s);
}

void BowState::detail::StripTemperingSuffixes(std::string& name) {
    TrimTrailingSpaces(name);

    for (;;) {
        bool canStrip = true;
        std::size_t open = std::string::npos;

        if (name.size() < 3 || name.back() != ')') {
            canStrip = false;
        }

        if (canStrip) {
            open = name.rfind('(');
            if (open == std::string::npos || open == 0 || name[open - 1] != ' ') {
                canStrip = false;
            }
        }

        std::string_view inside{};
        if (canStrip) {
            inside = std::string_view(name.data() + open + 1, name.size() - open - 2);
            if (!IsTemperingTag(inside)) {
                canStrip = false;
            }
        }

        if (!canStrip) {
            break;
        }

        name.erase(open - 1);
        TrimTrailingSpaces(name);
    }

    TrimTrailingSpaces(name);
}

void BowState::detail::ApplyChosenTagToInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
    if (!base || !extra) {
        return;
    }

    if (IntegratedBow::GetBowConfig().noChosenTag) return;

    auto* tdd = extra->GetExtraTextDisplayData();

    const char* cstr = nullptr;

    if (tdd) {
        cstr = tdd->displayName.c_str();
    }

    if (!cstr || !*cstr) {
        cstr = extra->GetDisplayName(base);
    }

    if (!cstr || !*cstr) {
        cstr = base->GetName();
    }

    if (!cstr || !*cstr) {
        return;
    }

    constexpr std::string_view chosenTag{" (chosen)"};
    std::string curName{cstr};

    RemoveChosenTagInplace(curName);
    StripTemperingSuffixes(curName);

    const auto cleanedName = curName;
    if (!curName.empty()) {
        curName += chosenTag;
    } else {
        curName = "(chosen)";
    }

    if (!tdd) {
        tdd = new RE::ExtraTextDisplayData(base, 1.0f);  // NOSONAR Lifetime é gerenciado pelo engine.
        if (!tdd) {
            return;
        }
        extra->Add(tdd);
    }

    tdd->SetName(curName.c_str());
}

void BowState::detail::RemoveChosenTagFromInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
    if (!base || !extra) {
        return;
    }

    auto* tdd = extra->GetExtraTextDisplayData();

    if (!tdd) {
        return;
    }

    const char* cstr = tdd->displayName.c_str();

    if (!cstr || !*cstr) {
        extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
        return;
    }

    std::string curName = cstr;

    RemoveChosenTagInplace(curName);
    StripTemperingSuffixes(curName);

    if (curName.empty()) {
        extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
    } else {
        tdd->SetName(curName.c_str());
    }
}

RE::BSFixedString BowState::detail::GetAttackUserEvent() {
    static RE::BSFixedString ev{"Right Attack/Block"};
    return ev;
}

RE::ButtonEvent* BowState::detail::MakeAttackButtonEvent(float value, float heldSecs) {
    return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, GetAttackUserEvent(), kAttackMouseIdCode, value, heldSecs);
}

void BowState::detail::EnqueueSyntheticAttack(RE::ButtonEvent* ev) {
    if (!ev) {
        return;
    }

    auto& st = GetSyntheticInputState();
    {
        std::scoped_lock lk(st.mutex);
        st.pending.push(ev);
    }
}

RE::InputEvent* BowState::detail::FlushSyntheticInput(RE::InputEvent* head) {
    auto& st = GetSyntheticInputState();

    std::queue<RE::ButtonEvent*> local;
    {
        std::scoped_lock lk(st.mutex);
        local.swap(st.pending);
    }

    if (local.empty()) {
        return head;
    }

    RE::InputEvent* synthHead = nullptr;
    RE::InputEvent* synthTail = nullptr;

    while (!local.empty()) {
        auto* ev = local.front();
        local.pop();
        if (!ev) {
            continue;
        }

        ev->next = nullptr;

        if (!synthHead) {
            synthHead = ev;
            synthTail = ev;
        } else {
            synthTail->next = ev;
            synthTail = ev;
        }
    }

    if (!head) {
        return synthHead;
    }

    synthTail->next = head;

    return synthHead;
}

void BowState::detail::DispatchAttackButtonEvent(RE::ButtonEvent* ev) { EnqueueSyntheticAttack(ev); }

BowState::detail::SyntheticInputState& BowState::detail::GetSyntheticInputState() {
    static SyntheticInputState s;  // NOSONAR
    return s;
}

BowState::IntegratedBowState& BowState::Get() {
    static IntegratedBowState s;  // NOSONAR
    return s;
}

void BowState::LoadChosenBow(RE::TESObjectWEAP* bow) {
    auto& st = Get();

    st.chosenBow.base = nullptr;
    st.chosenBow.extra = nullptr;

    if (!bow) {
        return;
    }

    RE::TESBoundObject* base = bow->As<RE::TESBoundObject>();
    if (!base) {
        return;
    }

    st.chosenBow.base = base;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

    constexpr const char* kChosenTag = " (chosen)";
    constexpr std::size_t kTagLen = 9;

    for (auto const& [obj, data] : inventory) {
        if (obj != base) {
            continue;
        }

        auto const& entryPtr = data.second;

        auto const* entry = entryPtr.get();
        if (!entry) {
            continue;
        }

        if (!entry || !entry->extraLists) {
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            const char* dispName = extra->GetDisplayName(obj);
            if (!dispName || !*dispName) {
                continue;
            }

            const std::size_t len = std::strlen(dispName);
            if (len < kTagLen) {
                continue;
            }

            if (std::memcmp(dispName + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                st.chosenBow.extra = extra;

                return;
            }
        }
    }
}

void BowState::ClearChosenBow() {
    auto& st = Get();
    auto& cfg = IntegratedBow::GetBowConfig();

    if (st.chosenBow.base && st.chosenBow.extra) {
        detail::RemoveChosenTagFromInstance(st.chosenBow.base, st.chosenBow.extra);
    }

    st.chosenBow.base = nullptr;
    st.chosenBow.extra = nullptr;

    cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
    cfg.Save();
}

bool BowState::EnsureChosenBowInInventory() {
    auto& st = Get();
    auto& cfg = IntegratedBow::GetBowConfig();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    if (!st.chosenBow.base) {
        if (const auto formId = cfg.chosenBowFormID.load(std::memory_order_relaxed); formId != 0) {
            if (auto* bowForm = RE::TESForm::LookupByID<RE::TESObjectWEAP>(formId)) {
                LoadChosenBow(bowForm);
            }
        }

        if (!st.chosenBow.base) {
            return false;
        }
    }

    auto const* const chosenBase = st.chosenBow.base;
    if (!chosenBase) {
        return false;
    }

    auto inventory = player->GetInventory([chosenBase](RE::TESBoundObject& obj) { return &obj == chosenBase; });

    constexpr const char* kChosenTag = " (chosen)";
    constexpr std::size_t kTagLen = 9;

    bool anyFound = false;
    RE::ExtraDataList* foundAnyExtra = nullptr;
    RE::ExtraDataList* foundExact = nullptr;
    RE::ExtraDataList* foundTagged = nullptr;

    for (auto const& [obj, data] : inventory) {
        if (obj != chosenBase) {
            continue;
        }

        if (auto const count = data.first; count > 0) {
            anyFound = true;
        }

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry) {
            continue;
        }

        if (!entry->extraLists || entry->extraLists->empty()) {
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            if (!foundAnyExtra) {
                foundAnyExtra = extra;
            }

            const char* dispName = extra->GetDisplayName(obj);
            if (!dispName || !*dispName) {
                continue;
            }

            if (st.chosenBow.extra && extra == st.chosenBow.extra) {
                foundExact = extra;

                break;
            }

            const std::size_t len = std::strlen(dispName);
            if (len >= kTagLen && std::memcmp(dispName + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                foundTagged = extra;
            }
        }

        if (foundExact) {
            break;
        }
    }

    if (foundExact) {
        return true;
    }

    if (foundTagged) {
        st.chosenBow.extra = foundTagged;

        return true;
    }

    if (foundAnyExtra) {
        st.chosenBow.extra = foundAnyExtra;

        BowState::detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);

        if (auto const* bow = st.chosenBow.base->As<RE::TESObjectWEAP>()) {
            cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);

        } else {
            cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        }

        cfg.Save();
        return true;
    }

    if (anyFound) {
        st.chosenBow.extra = nullptr;

        if (auto const* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr) {
            cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
            cfg.Save();
        }

        return true;
    }

    ClearChosenBow();

    return false;
}

void BowState::SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra) {
    auto& cfg = IntegratedBow::GetBowConfig();
    auto& st = Get();

    const auto newBase = bow ? bow->As<RE::TESBoundObject>() : nullptr;

    if (RE::TESBoundObject const* base = newBase; st.chosenBow.base == base && st.chosenBow.extra == extra) {
        if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra)) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, live);
        } else {
            RemoveChosenTagFromAnyTaggedInstance(st.chosenBow.base);
        }
        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        cfg.Save();

        return;
    }

    if (st.chosenBow.base && st.chosenBow.extra) {
        if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra)) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, live);
        } else {
            RemoveChosenTagFromAnyTaggedInstance(st.chosenBow.base);
        }
    }

    st.chosenBow.base = newBase;
    st.chosenBow.extra = extra;

    const auto newId = bow ? bow->GetFormID() : 0u;
    cfg.chosenBowFormID.store(newId, std::memory_order_relaxed);
    cfg.Save();
    if (!st.chosenBow.extra) {
        EnsureChosenBowInInventory();
    }
    detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);
}

RE::ExtraDataList* BowState::FindAnyInstanceExtraForBase(RE::TESBoundObject* base) {
    if (!base) {
        return nullptr;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return nullptr;
    }

    auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

    for (auto const& [obj, data] : inventory) {
        if (obj != base) {
            continue;
        }

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry) {
            continue;
        }
        if (!entry->extraLists) {
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            return extra;
        }
    }

    return nullptr;
}

void BowState::CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out) {
    out.clear();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });

    for (auto const& [obj, data] : inventory) {
        auto* armor = obj->As<RE::TESObjectARMO>();
        if (!armor) {
            continue;
        }

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry || !entry->extraLists) {
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
            const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);

            if (worn || wornLeft) {
                out.push_back(ExtraEquippedItem{armor->As<RE::TESBoundObject>(), extra});
            }
        }
    }
}

std::vector<BowState::ExtraEquippedItem> BowState::DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                                     const std::vector<ExtraEquippedItem>& after) {
    std::vector<ExtraEquippedItem> removed;

    for (auto const& b : before) {
        bool stillWorn = false;

        for (auto const& a : after) {
            if (a.base == b.base && a.extra == b.extra) {
                stillWorn = true;
                break;
            }
        }

        if (!stillWorn) {
            removed.push_back(b);
        }
    }

    return removed;
}

void BowState::ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* equipMgr) {
    if (!actor || !equipMgr) {
        return;
    }

    auto& st = Get();

    for (auto const& item : st.prevExtraEquipped) {
        if (!item.base) {
            continue;
        }

        const bool isArmor = (item.base->GetFormType() == RE::FormType::Armor);

        const bool queue = false;
        const bool force = !isArmor;
        const bool applyNow = isArmor;

        equipMgr->EquipObject(actor, item.base, item.extra, 1, nullptr, queue, force, true, applyNow);
    }

    st.prevExtraEquipped.clear();
}

void BowState::AppendPrevExtraEquipped(const ExtraEquippedItem& item) {
    auto& st = Get();
    st.prevExtraEquipped.push_back(item);
}

bool BowState::ContainsPrevExtraEquipped(const ExtraEquippedItem& item) {
    auto const& st = Get();
    return std::ranges::any_of(st.prevExtraEquipped,
                               [&](auto const& e) { return e.base == item.base && e.extra == item.extra; });
}

void BowState::ApplyHiddenItemsPatch(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                     const std::vector<RE::FormID>& hiddenFormIDs) {
    if (!player || !equipMgr) {
        return;
    }
    if (hiddenFormIDs.empty()) {
        return;
    }

    auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });

    for (auto const& [obj, data] : inventory) {
        auto* armor = obj->As<RE::TESObjectARMO>();
        if (!armor) {
            continue;
        }

        if (const auto armorId = armor->GetFormID(); !std::ranges::binary_search(hiddenFormIDs, armorId)) {
            continue;
        }

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry || !entry->extraLists) {
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
            if (const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft); !worn && !wornLeft) {
                continue;
            }

            ExtraEquippedItem item{armor->As<RE::TESBoundObject>(), extra};
            if (ContainsPrevExtraEquipped(item)) {
                continue;
            }

            equipMgr->UnequipObject(player, item.base, item.extra, 1, nullptr, true, true, true, false, nullptr);
            AppendPrevExtraEquipped(item);
        }
    }
}

RE::TESAmmo* BowState::GetPreferredArrow() {
    auto const& cfg = IntegratedBow::GetBowConfig();
    const auto formId = cfg.preferredArrowFormID.load(std::memory_order_relaxed);

    if (formId == 0) {
        return nullptr;
    }

    auto* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(formId);

    return ammo;
}

void BowState::SetPreferredArrow(RE::TESAmmo* ammo) {
    auto& cfg = IntegratedBow::GetBowConfig();
    const auto newId = ammo ? ammo->GetFormID() : 0u;

    cfg.preferredArrowFormID.store(newId, std::memory_order_relaxed);
    cfg.Save();
}

void BowState::RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                         IntegratedBowState& st) {
    if (!player || !equipMgr) {
        return;
    }

    auto const& cfg = IntegratedBow::GetBowConfig();

    const bool doSkipReturn = cfg.skipEquipReturnToMeleePatch.load(std::memory_order_relaxed);
    ScopedSkipEquipReturn skipGuard(doSkipReturn, player);

    BowInput::ForceAllowUnequip();

    RE::TESBoundObject* rightBase = st.prevRight.base;
    RE::ExtraDataList* rightExtra = st.prevRight.extra;

    if (!rightBase && st.prevRightFormID != 0) {
        if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevRightFormID)) {
            rightBase = form;

            rightExtra = FindAnyInstanceExtraForBase(form);
        }
    }

    if (rightBase) {
        if (rightExtra) {
            if (auto* live = ResolveLiveExtra(rightBase, rightExtra)) {
                rightExtra = live;
            } else {
                rightExtra = FindAnyInstanceExtraForBase(rightBase);
            }
        }

        const bool queue = (rightExtra == nullptr);

        equipMgr->EquipObject(player, rightBase, rightExtra, 1, nullptr, queue, false, true, false);
    }

    RE::TESBoundObject* leftBase = st.prevLeft.base;
    RE::ExtraDataList* leftExtra = st.prevLeft.extra;

    if (!leftBase && st.prevLeftFormID != 0) {
        if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevLeftFormID)) {
            leftBase = form;

            leftExtra = FindAnyInstanceExtraForBase(form);
        }
    }

    if (leftBase) {
        if (leftExtra) {
            if (auto* live = ResolveLiveExtra(leftBase, leftExtra)) {
                leftExtra = live;
            } else {
                leftExtra = FindAnyInstanceExtraForBase(leftBase);
            }
        }

        const bool queue = (leftExtra == nullptr);

        equipMgr->EquipObject(player, leftBase, leftExtra, 1, nullptr, queue, false, true, false);
    }

    if (!rightBase && !leftBase && st.chosenBow.base) {
        equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true, false,
                                nullptr);
    }

    if (auto* prevAmmo = st.prevAmmo) {
        equipMgr->EquipObject(player, prevAmmo, nullptr, 1, nullptr, true, false, true, false);
    } else {
        auto* preferred = GetPreferredArrow();
        if (preferred) {
            equipMgr->UnequipObject(player, preferred, nullptr, 1, nullptr, true, true, true, false, nullptr);
        }
    }

    st.prevAmmo = nullptr;
    st.isUsingBow = false;

    if (st.prevExtraEquipped.empty()) {
        ClearPrevWeapons();
        ClearPrevExtraEquipped();
        return;
    }

    st.pendingFinalizeExtras = true;
    st.pendingFinalizeExtrasTimer = 0.0f;
    st.pendingDesiredRight = st.prevRight.base;
    st.pendingDesiredLeft = st.prevLeft.base;

    ClearPrevWeapons();
}

void BowState::UpdateDeferredFinalize(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr, float dt) {
    auto& st = Get();
    if (!st.pendingFinalizeExtras) {
        return;
    }

    st.pendingFinalizeExtrasTimer += dt;

    const bool rightOK = IsHandReady(player, st.pendingDesiredRight, false);
    const bool leftOK = IsHandReady(player, st.pendingDesiredLeft, true);
    if (const bool timedOut = (st.pendingFinalizeExtrasTimer >= 2.0f); !timedOut && !(rightOK && leftOK)) {
        return;
    }

    ReequipPrevExtraEquipped(player, equipMgr);

    st.pendingFinalizeExtras = false;
    st.pendingFinalizeExtrasTimer = 0.0f;
    st.pendingDesiredRight = nullptr;
    st.pendingDesiredLeft = nullptr;

    ClearPrevExtraEquipped();
}