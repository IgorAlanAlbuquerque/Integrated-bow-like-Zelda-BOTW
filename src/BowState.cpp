#include "BowState.h"

namespace {
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
}

bool BowState::detail::IsTemperingTag(std::string_view inside) {
    spdlog::info("[IBOW][STATE][NAME] IsTemperingTag enter inside='{}' len={}", inside, inside.size());
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
            spdlog::info("[IBOW][STATE][NAME] IsTemperingTag MATCH tag='{}' for inside='{}'", q, inside);
            return true;
        }
    }

    spdlog::info("[IBOW][STATE][NAME] IsTemperingTag no match for inside='{}'", inside);
    return false;
}

void BowState::detail::TrimTrailingSpaces(std::string& s) {
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
}

void BowState::detail::RemoveChosenTagInplace(std::string& s) {
    constexpr std::string_view chosenTag{" (chosen)"};

    spdlog::info("[IBOW][STATE][NAME] RemoveChosenTagInplace enter s='{}'", s);
    for (;;) {
        auto pos = s.find(chosenTag);
        if (pos == std::string::npos) {
            break;
        }

        s.erase(pos, chosenTag.size());
    }

    TrimTrailingSpaces(s);

    spdlog::info("[IBOW][STATE][NAME] RemoveChosenTagInplace done result='{}'", s);
}

void BowState::detail::StripTemperingSuffixes(std::string& name) {
    spdlog::info("[IBOW][STATE][NAME] StripTemperingSuffixes enter name='{}'", name);
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

        spdlog::info("[IBOW][STATE][NAME] StripTemperingSuffixes stripping suffix '({})' at open={} from name='{}'",
                     inside, open, name);

        name.erase(open - 1);
        TrimTrailingSpaces(name);
    }

    TrimTrailingSpaces(name);
    spdlog::info("[IBOW][STATE][NAME] StripTemperingSuffixes done result='{}'", name);
}

void BowState::detail::ApplyChosenTagToInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
    if (!base || !extra) {
        spdlog::warn("[IBOW][STATE][CHOSEN_TAG] ApplyChosenTagToInstance base/extra null base={} extra={}", (void*)base,
                     (void*)extra);
        return;
    }

    auto* tdd = extra->GetExtraTextDisplayData();
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] extra->GetExtraTextDisplayData() tdd={}", (void*)tdd);

    const char* cstr = nullptr;

    if (tdd) {
        cstr = tdd->displayName.c_str();
    }

    if (!cstr || !*cstr) {
        cstr = extra->GetDisplayName(base);
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] source=tdd displayName='{}'", cstr ? cstr : "");
    }

    if (!cstr || !*cstr) {
        cstr = base->GetName();
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] source=base->GetName '{}'", cstr ? cstr : "");
    }

    if (!cstr || !*cstr) {
        spdlog::warn("[IBOW][STATE][CHOSEN_TAG] no usable name source; abort");
        return;
    }

    constexpr std::string_view chosenTag{" (chosen)"};
    std::string curName{cstr};
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] current name before cleanup='{}'", curName);
    RemoveChosenTagInplace(curName);
    StripTemperingSuffixes(curName);

    const auto cleanedName = curName;
    if (!curName.empty()) {
        curName += chosenTag;
    } else {
        curName = "(chosen)";
    }
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] cleaned='{}' finalWithTag='{}'", cleanedName, curName);

    if (!tdd) {
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] no tdd -> creating ExtraTextDisplayData");
        tdd = new RE::ExtraTextDisplayData(base, 1.0f);  // NOSONAR Lifetime é gerenciado pelo engine.
        if (!tdd) {
            spdlog::warn("[IBOW][STATE][CHOSEN_TAG] new ExtraTextDisplayData failed -> abort");
            return;
        }
        extra->Add(tdd);
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] created tdd={} and added to extra", (void*)tdd);
    }

    tdd->SetName(curName.c_str());
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] SetName done -> '{}'", curName);
}

void BowState::detail::RemoveChosenTagFromInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
    if (!base || !extra) {
        spdlog::warn("[IBOW][STATE][CHOSEN_TAG] RemoveChosenTagFromInstance base/extra null base={} extra={}",
                     (void*)base, (void*)extra);
        return;
    }

    auto* tdd = extra->GetExtraTextDisplayData();
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] extra->GetExtraTextDisplayData() tdd={}", (void*)tdd);

    if (!tdd) {
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] no tdd -> nothing to remove");
        return;
    }

    const char* cstr = tdd->displayName.c_str();
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] tdd displayName='{}'", cstr ? cstr : "");

    if (!cstr || !*cstr) {
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] empty tdd name -> removing TextDisplayData extra");
        extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
        return;
    }

    std::string curName = cstr;
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] current name before cleanup='{}'", curName);

    RemoveChosenTagInplace(curName);
    StripTemperingSuffixes(curName);
    spdlog::info("[IBOW][STATE][CHOSEN_TAG] cleaned result='{}'", curName);

    if (curName.empty()) {
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] cleaned empty -> removing TextDisplayData extra");
        extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
    } else {
        spdlog::info("[IBOW][STATE][CHOSEN_TAG] SetName -> '{}'", curName);
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
        spdlog::info("[IBOW][STATE][SYN] EnqueueSyntheticAttack ev=null -> return");
        return;
    }

    auto& st = GetSyntheticInputState();
    {
        std::scoped_lock lk(st.mutex);
        st.pending.push(ev);
        spdlog::info("[IBOW][STATE][SYN] Enqueued attack event ev={} pendingSize={}", (void*)ev, st.pending.size());
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
        spdlog::info("[IBOW][STATE][SYN] FlushSyntheticInput: nothing to flush. head={}", (void*)head);
        return head;
    }

    spdlog::info("[IBOW][STATE][SYN] FlushSyntheticInput: flushing N={} head={}", local.size(), (void*)head);

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
    spdlog::info("[IBOW][STATE][SYN] FlushSyntheticInput: built synth list synthHead={} synthTail={}", (void*)synthHead,
                 (void*)synthTail);

    if (!head) {
        spdlog::info("[IBOW][STATE][SYN] FlushSyntheticInput: returning synthHead only");
        return synthHead;
    }

    synthTail->next = head;
    spdlog::info("[IBOW][STATE][SYN] FlushSyntheticInput: returning synthHead (prepended before head)");

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
    spdlog::info("[IBOW][STATE][CHOSEN] LoadChosenBow enter bow={} formID=0x{:08X}", (void*)bow,
                 bow ? bow->GetFormID() : 0u);
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
        spdlog::info("[IBOW][STATE][CHOSEN] Checking entry for item {}.", (void*)obj);

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
                spdlog::info("[IBOW][STATE][CHOSEN] LoadChosenBow FOUND tagged extra={} base={}", (void*)extra,
                             (void*)base);

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
    spdlog::info("[IBOW][STATE][CHOSEN] EnsureChosenBowInInventory enter base={} extra={} cfgFormID=0x{:08X}",
                 (void*)st.chosenBow.base, (void*)st.chosenBow.extra,
                 cfg.chosenBowFormID.load(std::memory_order_relaxed));

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("[IBOW][STATE][CHOSEN] No player found. Returning false.");

        return false;
    }

    if (!st.chosenBow.base) {
        spdlog::info(
            "[IBOW][STATE][CHOSEN] Chosen bow base not set (base={} extra={}). Trying to load from config "
            "(FormID=0x{:08X}).",
            (void*)st.chosenBow.base, (void*)st.chosenBow.extra, cfg.chosenBowFormID.load(std::memory_order_relaxed));

        if (const auto formId = cfg.chosenBowFormID.load(std::memory_order_relaxed); formId != 0) {
            spdlog::info("[IBOW][STATE][CHOSEN] Loading chosen bow with FormID 0x{:08X}", formId);

            if (auto* bowForm = RE::TESForm::LookupByID<RE::TESObjectWEAP>(formId)) {
                LoadChosenBow(bowForm);
            }
        }

        if (!st.chosenBow.base) {
            spdlog::warn("[IBOW][STATE][CHOSEN] Failed to load chosen bow BASE from config. Returning false.");

            return false;
        }
    }

    auto const* const chosenBase = st.chosenBow.base;
    if (!chosenBase) {
        spdlog::warn("[IBOW][STATE][CHOSEN] Chosen bow base is null. Returning false.");

        return false;
    }

    spdlog::info("[IBOW][STATE][CHOSEN] Searching for chosen bow in inventory...");

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
            spdlog::info("[IBOW][STATE][CHOSEN] entry has no extraLists -> skipping");
            continue;
        }
        spdlog::info("[IBOW][STATE][CHOSEN] Checking entry for item {}.", (void*)obj);
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
            spdlog::info("[IBOW][STATE][CHOSEN] Checking extra item with displayName='{}'", dispName);

            if (st.chosenBow.extra && extra == st.chosenBow.extra) {
                foundExact = extra;
                spdlog::info("[IBOW][STATE][CHOSEN] Found exact match for chosen bow extra.");

                break;
            }

            const std::size_t len = std::strlen(dispName);
            if (len >= kTagLen && std::memcmp(dispName + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                foundTagged = extra;
                spdlog::info("[IBOW][STATE][CHOSEN] Found tagged match for chosen bow extra (tagged with '{}').",
                             kChosenTag);
            }
        }

        if (foundExact) {
            spdlog::info("[IBOW][STATE][CHOSEN] Exact match found, breaking loop.");

            break;
        }
    }

    if (foundExact) {
        spdlog::info("[IBOW][STATE][CHOSEN] Exact match found in inventory. Returning true.");

        return true;
    }

    if (foundTagged) {
        st.chosenBow.extra = foundTagged;
        spdlog::info("[IBOW][STATE][CHOSEN] Tagged match found, updating extra. Returning true.");

        return true;
    }

    if (foundAnyExtra) {
        st.chosenBow.extra = foundAnyExtra;
        spdlog::info("[IBOW][STATE][CHOSEN] Any match found, updating extra and applying chosen tag.");

        BowState::detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);

        if (auto const* bow = st.chosenBow.base->As<RE::TESObjectWEAP>()) {
            cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
            spdlog::info("[IBOW][STATE][CHOSEN] Storing new bow FormID 0x{:08X}", bow->GetFormID());

        } else {
            cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
            spdlog::warn("[IBOW][STATE][CHOSEN] No valid bow found. Setting FormID to 0.");
        }

        cfg.Save();
        return true;
    }

    if (anyFound) {
        spdlog::info(
            "[IBOW][STATE][CHOSEN] Chosen bow base found in inventory but no matching extra data. "
            "Clearing chosen bow.");
        st.chosenBow.extra = nullptr;

        if (auto const* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr) {
            cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
            cfg.Save();
        }

        return true;
    }

    ClearChosenBow();
    spdlog::info("[IBOW][STATE][CHOSEN] No valid bow found in inventory, clearing chosen bow. Returning false.");

    return false;
}

void BowState::SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra) {
    auto& cfg = IntegratedBow::GetBowConfig();
    auto& st = Get();

    const auto newBase = bow ? bow->As<RE::TESBoundObject>() : nullptr;
    spdlog::info(
        "[IBOW][STATE][CHOSEN] SetChosenBow enter bow={} newBase={} extra={} curBase={} curExtra={} "
        "curFormID=0x{:08X}",
        (void*)bow, (void*)newBase, (void*)extra, (void*)st.chosenBow.base, (void*)st.chosenBow.extra,
        cfg.chosenBowFormID.load(std::memory_order_relaxed));

    if (RE::TESBoundObject const* base = newBase; st.chosenBow.base == base && st.chosenBow.extra == extra) {
        spdlog::info("[IBOW][STATE][CHOSEN] toggling OFF (same instance). Removing tag and clearing.");

        if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra)) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, live);
        } else {
            spdlog::warn(
                "[IBOW][STATE][CHOSEN] prev extra is not in current extraLists -> skip pointer remove; scan tagged");

            RemoveChosenTagFromAnyTaggedInstance(st.chosenBow.base);
        }
        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        cfg.Save();
        spdlog::info("[IBOW][STATE][CHOSEN] toggling OFF done. cfgFormID=0x{:08X}",
                     cfg.chosenBowFormID.load(std::memory_order_relaxed));

        return;
    }

    if (st.chosenBow.base && st.chosenBow.extra) {
        spdlog::info("[IBOW][STATE][CHOSEN] removing previous chosen tag prevBase={} prevExtra={}",
                     (void*)st.chosenBow.base, (void*)st.chosenBow.extra);

        if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra)) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, live);
        } else {
            spdlog::warn(
                "[IBOW][STATE][CHOSEN] prev extra is not in current extraLists -> skip pointer remove; scan tagged");

            RemoveChosenTagFromAnyTaggedInstance(st.chosenBow.base);
        }
    }

    st.chosenBow.base = newBase;
    st.chosenBow.extra = extra;

    spdlog::info("[IBOW][STATE][CHOSEN] setting ON newBase={} newExtra={} (apply tag)", (void*)st.chosenBow.base,
                 (void*)st.chosenBow.extra);

    const auto newId = bow ? bow->GetFormID() : 0u;
    cfg.chosenBowFormID.store(newId, std::memory_order_relaxed);
    cfg.Save();
    if (!st.chosenBow.extra) {
        EnsureChosenBowInInventory();
    }
    detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);
    spdlog::info("[IBOW][STATE][CHOSEN] setting ON done. cfgFormID=0x{:08X}", newId);
}

RE::ExtraDataList* BowState::FindAnyInstanceExtraForBase(RE::TESBoundObject* base) {
    if (!base) {
        spdlog::warn("[IBOW][STATE][FIND_EXTRA] base=null -> return null");

        return nullptr;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("[IBOW][STATE][FIND_EXTRA] player=null -> return null");

        return nullptr;
    }

    auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });
    spdlog::info("[IBOW][STATE][FIND_EXTRA] inventory filtered count={}", inventory.size());

    for (auto const& [obj, data] : inventory) {
        if (obj != base) {
            continue;
        }

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry) {
            spdlog::info("[IBOW][STATE][FIND_EXTRA] entry=null for obj={} (unexpected) -> continue", (void*)obj);

            continue;
        }
        if (!entry->extraLists) {
            spdlog::info("[IBOW][STATE][FIND_EXTRA] entry has no extraLists for obj={} -> continue", (void*)obj);

            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            spdlog::info("[IBOW][STATE][FIND_EXTRA] FOUND extra={} (returning)", (void*)extra);

            return extra;
        }
    }

    spdlog::info("[IBOW][STATE][FIND_EXTRA] no extra found for base id=0x{:08X} -> return null",
                 base ? base->GetFormID() : 0u);

    return nullptr;
}

void BowState::CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out) {
    spdlog::info("[IBOW][STATE][SNAPSHOT] CaptureWornArmorSnapshot enter");

    out.clear();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("[IBOW][STATE][SNAPSHOT] player=null, returning");

        return;
    }

    spdlog::info("[IBOW][STATE][SNAPSHOT] player found, fetching inventory...");

    auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });
    spdlog::info("[IBOW][STATE][SNAPSHOT] Inventory fetched, item count={}", inventory.size());

    for (auto const& [obj, data] : inventory) {
        auto* armor = obj->As<RE::TESObjectARMO>();
        if (!armor) {
            spdlog::info("[IBOW][STATE][SNAPSHOT] not an armor item, skipping");

            continue;
        }

        const auto armorId = armor->GetFormID();
        spdlog::info("[IBOW][STATE][SNAPSHOT] armor item found, id=0x{:08X} name='{}'", armorId,
                     armor->GetName() ? armor->GetName() : "");

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry || !entry->extraLists) {
            spdlog::info("[IBOW][STATE][SNAPSHOT] armor entry or extraLists null for armor=0x{:08X}, skipping",
                         armorId);

            continue;
        }
        spdlog::info("[IBOW][STATE][SNAPSHOT] scanning extraLists for armor=0x{:08X}", armorId);

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                spdlog::info("[IBOW][STATE][SNAPSHOT] extra=null, skipping");

                continue;
            }

            const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
            const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);

            if (worn || wornLeft) {
                spdlog::info("[IBOW][STATE][SNAPSHOT] armor=0x{:08X} has extra worn/wornLeft. Adding to snapshot.",
                             armorId);

                out.push_back(ExtraEquippedItem{armor->As<RE::TESBoundObject>(), extra});
            }
        }
    }
    spdlog::info("[IBOW][STATE][SNAPSHOT] Snapshot captured with {} worn items.", out.size());
}

std::vector<BowState::ExtraEquippedItem> BowState::DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                                     const std::vector<ExtraEquippedItem>& after) {
    spdlog::info("[IBOW][STATE][DIFF] DiffArmorSnapshot enter beforeCount={} afterCount={}", before.size(),
                 after.size());
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
            spdlog::info("[IBOW][STATE][DIFF] removed base={} extra={} id=0x{:08X} name='{}'", (void*)b.base,
                         (void*)b.extra, b.base ? b.base->GetFormID() : 0u, b.base ? b.base->GetName() : "");
            removed.push_back(b);
        }
    }
    spdlog::info("[IBOW][STATE][DIFF] DiffArmorSnapshot done removedCount={}", removed.size());
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

        equipMgr->EquipObject(actor, item.base, item.extra, 1, nullptr, true, true, true, false);
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
    spdlog::info("[IBOW][STATE][HIDDEN] enter player={} equipMgr={} hiddenCount={}", (void*)player, (void*)equipMgr,
                 hiddenFormIDs.size());
    if (!player || !equipMgr) {
        spdlog::warn("[IBOW][STATE][HIDDEN] player/equipMgr null -> return");

        return;
    }
    if (hiddenFormIDs.empty()) {
        spdlog::info("[IBOW][STATE][HIDDEN] hiddenFormIDs empty -> return");

        return;
    }

    auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });
    spdlog::info("[IBOW][STATE][HIDDEN] inventory total entries={}", inventory.size());

    for (auto const& [obj, data] : inventory) {
        auto* armor = obj->As<RE::TESObjectARMO>();
        if (!armor) {
            continue;
        }

        const auto armorId = armor->GetFormID();
        if (!std::ranges::binary_search(hiddenFormIDs, armorId)) {
            continue;
        }

        spdlog::info("[IBOW][STATE][HIDDEN] matched armor id=0x{:08X} name='{}' obj={}", armorId,
                     armor->GetName() ? armor->GetName() : "", (void*)armor);

        auto const& entryPtr = data.second;
        auto const* entry = entryPtr.get();
        if (!entry || !entry->extraLists) {
            spdlog::info("[IBOW][STATE][HIDDEN] armor entry missing extraLists -> continue (id=0x{:08X})", armorId);
            continue;
        }

        for (auto* extra : *entry->extraLists) {
            if (!extra) {
                continue;
            }

            const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
            const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);

            if (!worn && !wornLeft) {
                continue;
            }

            ExtraEquippedItem item{armor->As<RE::TESBoundObject>(), extra};
            if (ContainsPrevExtraEquipped(item)) {
                spdlog::info("[IBOW][STATE][HIDDEN] already in prevExtraEquipped -> skip armor=0x{:08X} extra={}",
                             armorId, (void*)extra);
                continue;
            }

            spdlog::info("[IBOW][STATE][HIDDEN] Unequip hidden armor id=0x{:08X} extra={} worn={} wornLeft={}", armorId,
                         (void*)extra, worn, wornLeft);

            equipMgr->UnequipObject(player, item.base, item.extra, 1, nullptr, true, true, true, false, nullptr);
            AppendPrevExtraEquipped(item);
        }
    }
    spdlog::info("[IBOW][STATE][HIDDEN] done prevExtraEquippedCount={}", Get().prevExtraEquipped.size());
}

RE::TESAmmo* BowState::GetPreferredArrow() {
    auto const& cfg = IntegratedBow::GetBowConfig();
    const auto formId = cfg.preferredArrowFormID.load(std::memory_order_relaxed);
    spdlog::info("[IBOW][STATE][ARROW] GetPreferredArrow stored formId=0x{:08X}", formId);
    if (formId == 0) {
        spdlog::info("[IBOW][STATE][ARROW] GetPreferredArrow formId=0 -> return null");

        return nullptr;
    }

    auto* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(formId);
    spdlog::info("[IBOW][STATE][ARROW] GetPreferredArrow LookupByID 0x{:08X} -> ammo={} name='{}'", formId, (void*)ammo,
                 ammo && ammo->GetName() ? ammo->GetName() : "");

    return ammo;
}

void BowState::SetPreferredArrow(RE::TESAmmo* ammo) {
    auto& cfg = IntegratedBow::GetBowConfig();
    const auto newId = ammo ? ammo->GetFormID() : 0u;
    spdlog::info("[IBOW][STATE][ARROW] SetPreferredArrow enter ammo={} newId=0x{:08X} name='{}'", (void*)ammo, newId,
                 ammo && ammo->GetName() ? ammo->GetName() : "");

    cfg.preferredArrowFormID.store(newId, std::memory_order_relaxed);
    cfg.Save();
    spdlog::info("[IBOW][STATE][ARROW] SetPreferredArrow saved preferredArrowFormID=0x{:08X}", newId);
}

void BowState::RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                         IntegratedBowState& st) {
    spdlog::info(
        "[IBOW][STATE][RESTORE] enter player={} equipMgr={} | "
        "prevR(base={},extra={},id=0x{:08X}) prevL(base={},extra={},id=0x{:08X}) "
        "chosen(base={},extra={}) prevAmmo={} usingBow={} equipingBow={}",
        (void*)player, (void*)equipMgr, (void*)st.prevRight.base, (void*)st.prevRight.extra, st.prevRightFormID,
        (void*)st.prevLeft.base, (void*)st.prevLeft.extra, st.prevLeftFormID, (void*)st.chosenBow.base,
        (void*)st.chosenBow.extra, (void*)st.prevAmmo, st.isUsingBow, st.isEquipingBow);

    if (!player || !equipMgr) {
        spdlog::warn("[IBOW][STATE][RESTORE] player/equipMgr null -> return");

        return;
    }

    BowInput::ForceAllowUnequip();

    RE::TESBoundObject* rightBase = st.prevRight.base;
    RE::ExtraDataList* rightExtra = st.prevRight.extra;
    spdlog::info("[IBOW][STATE][RESTORE] RIGHT initial base={} extra={} id=0x{:08X}", (void*)rightBase,
                 (void*)rightExtra, rightBase ? rightBase->GetFormID() : 0u);

    if (!rightBase && st.prevRightFormID != 0) {
        spdlog::info("[IBOW][STATE][RESTORE] RIGHT base missing -> LookupByID 0x{:08X}", st.prevRightFormID);

        if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevRightFormID)) {
            rightBase = form;
            spdlog::info("[IBOW][STATE][RESTORE] RIGHT LookupByID OK form={} id=0x{:08X} name='{}'", (void*)form,
                         form->GetFormID(), form->GetName() ? form->GetName() : "");
            rightExtra = FindAnyInstanceExtraForBase(form);
            spdlog::info("[IBOW][STATE][RESTORE] RIGHT FindAnyInstanceExtraForBase -> extra={}", (void*)rightExtra);
        }
    }

    if (rightBase) {
        if (rightExtra) {
            if (auto* live = ResolveLiveExtra(rightBase, rightExtra)) {
                rightExtra = live;
            } else {
                spdlog::warn("[IBOW][STATE][RESTORE] RIGHT stale extra={} for base={} -> fallback lookup",
                             (void*)rightExtra, (void*)rightBase);
                rightExtra = FindAnyInstanceExtraForBase(rightBase);
                spdlog::info("[IBOW][STATE][RESTORE] RIGHT fallback FindAnyInstanceExtraForBase -> extra={}",
                             (void*)rightExtra);
            }
        }

        const bool queue = (rightExtra == nullptr);
        spdlog::info("[IBOW][STATE][RESTORE] RIGHT EquipObject base={} extra={} queue={} id=0x{:08X} name='{}'",
                     (void*)rightBase, (void*)rightExtra, queue, rightBase->GetFormID(),
                     rightBase->GetName() ? rightBase->GetName() : "");

        equipMgr->EquipObject(player, rightBase, rightExtra, 1, nullptr, queue, true, true, false);
    }

    RE::TESBoundObject* leftBase = st.prevLeft.base;
    RE::ExtraDataList* leftExtra = st.prevLeft.extra;
    spdlog::info("[IBOW][STATE][RESTORE] LEFT initial base={} extra={} id=0x{:08X}", (void*)leftBase, (void*)leftExtra,
                 leftBase ? leftBase->GetFormID() : 0u);

    if (!leftBase && st.prevLeftFormID != 0) {
        spdlog::info("[IBOW][STATE][RESTORE] LEFT base missing -> LookupByID 0x{:08X}", st.prevLeftFormID);
        if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevLeftFormID)) {
            leftBase = form;
            spdlog::info("[IBOW][STATE][RESTORE] LEFT LookupByID OK form={} id=0x{:08X} name='{}'", (void*)form,
                         form->GetFormID(), form->GetName() ? form->GetName() : "");
            leftExtra = FindAnyInstanceExtraForBase(form);
            spdlog::info("[IBOW][STATE][RESTORE] LEFT FindAnyInstanceExtraForBase -> extra={}", (void*)leftExtra);
        }
    }

    if (leftBase) {
        if (leftExtra) {
            if (auto* live = ResolveLiveExtra(leftBase, leftExtra)) {
                leftExtra = live;
            } else {
                spdlog::warn("[IBOW][STATE][RESTORE] LEFT stale extra={} for base={} -> fallback lookup",
                             (void*)leftExtra, (void*)leftBase);

                leftExtra = FindAnyInstanceExtraForBase(leftBase);
                spdlog::info("[IBOW][STATE][RESTORE] LEFT fallback FindAnyInstanceExtraForBase -> extra={}",
                             (void*)leftExtra);
            }
        }

        const bool queue = (leftExtra == nullptr);

        spdlog::info("[IBOW][STATE][RESTORE] LEFT EquipObject base={} extra={} queue={} id=0x{:08X} name='{}'",
                     (void*)leftBase, (void*)leftExtra, queue, leftBase->GetFormID(),
                     leftBase->GetName() ? leftBase->GetName() : "");

        equipMgr->EquipObject(player, leftBase, leftExtra, 1, nullptr, queue, true, true, false);
    }

    if (!rightBase && !leftBase && st.chosenBow.base) {
        spdlog::info(
            "[IBOW][STATE][RESTORE] both hands empty -> Unequip chosen bow base={} extra={} id=0x{:08X} name='{}'",
            (void*)st.chosenBow.base, (void*)st.chosenBow.extra, st.chosenBow.base->GetFormID(),
            st.chosenBow.base->GetName() ? st.chosenBow.base->GetName() : "");

        equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true, false,
                                nullptr);
    }

    if (auto* prevAmmo = st.prevAmmo) {
        spdlog::info("[IBOW][STATE][RESTORE] restoring prevAmmo ptr={} id=0x{:08X} name='{}'", (void*)prevAmmo,
                     prevAmmo->GetFormID(), prevAmmo->GetName() ? prevAmmo->GetName() : "");

        equipMgr->EquipObject(player, prevAmmo, nullptr, 1, nullptr, true, true, true, false);
    } else {
        auto* preferred = GetPreferredArrow();
        if (preferred) {
            spdlog::info("[IBOW][STATE][RESTORE] no prevAmmo -> Unequip preferredArrow ptr={} id=0x{:08X} name='{}'",
                         (void*)preferred, preferred->GetFormID(), preferred->GetName() ? preferred->GetName() : "");
            equipMgr->UnequipObject(player, preferred, nullptr, 1, nullptr, true, true, true, false, nullptr);
        }
    }
    spdlog::info("[IBOW][STATE][RESTORE] ReequipPrevExtraEquipped begin (count={})", st.prevExtraEquipped.size());
    ReequipPrevExtraEquipped(player, equipMgr);
    spdlog::info("[IBOW][STATE][RESTORE] ReequipPrevExtraEquipped done (count={})", st.prevExtraEquipped.size());

    st.prevAmmo = nullptr;
    st.isUsingBow = false;

    spdlog::info("[IBOW][STATE][RESTORE] clearing prev weapons/extras");
    ClearPrevWeapons();
    ClearPrevExtraEquipped();
    spdlog::info("[IBOW][STATE][RESTORE] done. usingBow={} equipingBow={} bowEquipped={} waitingAutoAfterEquip={}",
                 st.isUsingBow, st.isEquipingBow, st.bowEquipped.load(std::memory_order_relaxed),
                 st.waitingAutoAttackAfterEquip.load(std::memory_order_relaxed));
}