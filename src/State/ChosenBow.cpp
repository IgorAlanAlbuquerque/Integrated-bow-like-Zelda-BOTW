#include "ChosenBow.h"

#include <cstring>
#include <string>
#include <string_view>

#include "Config/Config.h"
#include "PCH.h"
#include "State/SessionState.h"

namespace BowState {
    namespace {
        constexpr std::array<std::string_view, 6> kQualityTags{"fine",     "superior", "exquisite",
                                                               "flawless", "epic",     "legendary"};

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        void TrimTrailingSpaces(std::string& s) {
#ifdef DEBUG
            const auto before = s.size();
#endif
            while (!s.empty() && s.back() == ' ') {
                s.pop_back();
            }
            BOW_DEBUG_LOG("[ChosenBow] TrimTrailingSpaces before='{}' after='{}' removed={}",
                          std::string(s.c_str(), before <= s.size() ? s.size() : s.size()), s, before - s.size());
        }

        bool IsTemperingTag(std::string_view inside) {
            BOW_DEBUG_LOG("[ChosenBow] IsTemperingTag checking '{}'", inside);

            for (auto q : kQualityTags) {
                if (inside.size() != q.size()) {
                    continue;
                }

                bool match = true;
                for (std::size_t i = 0; i < q.size(); ++i) {
                    if (static_cast<char>(std::tolower(static_cast<unsigned char>(inside[i]))) != q[i]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    BOW_DEBUG_LOG("[ChosenBow] IsTemperingTag matched '{}'", q);
                    return true;
                }
            }

            BOW_DEBUG_LOG("[ChosenBow] IsTemperingTag no match for '{}'", inside);
            return false;
        }

        void RemoveChosenTagInplace(std::string& s) {
            constexpr std::string_view tag{" (chosen)"};

            BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTagInplace input='{}'", s);

            for (;;) {
                auto pos = s.find(tag);
                if (pos == std::string::npos) {
                    break;
                }

                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTagInplace removing at pos={}", pos);
                s.erase(pos, tag.size());
            }

            TrimTrailingSpaces(s);
            BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTagInplace output='{}'", s);
        }

        void StripTemperingSuffixes(std::string& name) {
            BOW_DEBUG_LOG("[ChosenBow] StripTemperingSuffixes input='{}'", name);

            TrimTrailingSpaces(name);
            for (;;) {
                if (name.size() < 3 || name.back() != ')') {
                    break;
                }

                const auto open = name.rfind('(');
                if (open == std::string::npos || open == 0 || name[open - 1] != ' ') {
                    break;
                }

                const std::string_view inside{name.data() + open + 1, name.size() - open - 2};
                BOW_DEBUG_LOG("[ChosenBow] StripTemperingSuffixes candidate suffix='{}'", inside);

                if (!IsTemperingTag(inside)) {
                    break;
                }

                BOW_DEBUG_LOG("[ChosenBow] StripTemperingSuffixes removing suffix='{}'", inside);
                name.erase(open - 1);
                TrimTrailingSpaces(name);
            }

            TrimTrailingSpaces(name);
            BOW_DEBUG_LOG("[ChosenBow] StripTemperingSuffixes output='{}'", name);
        }

        void RemoveChosenTag(RE::TESBoundObject const* base, RE::ExtraDataList* extra) {
            BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag begin base={} extra={}", static_cast<const void*>(base),
                          static_cast<void*>(extra));

            if (!base || !extra) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag early return: base or extra null");
                return;
            }

            auto* tdd = extra->GetExtraTextDisplayData();
            if (!tdd) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag no ExtraTextDisplayData");
                return;
            }

            const char* cstr = tdd->displayName.c_str();
            if (!cstr || !*cstr) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag empty display name, removing TextDisplayData");
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
                return;
            }

            std::string name{cstr};
            BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag original display name='{}'", name);

            RemoveChosenTagInplace(name);
            StripTemperingSuffixes(name);

            if (name.empty()) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag final name empty, removing TextDisplayData");
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
            } else {
                BOW_DEBUG_LOG("[ChosenBow] RemoveChosenTag setting display name to '{}'", name);
                tdd->SetName(name.c_str());
            }
        }

        void RemoveTagFromAnyTaggedInstance(RE::TESBoundObject* base) {
            BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance begin base={}", static_cast<void*>(base));

            if (!base) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance early return: base null");
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance early return: player null");
                return;
            }

            auto inventory = player->GetInventory();
            BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance inventory entries={}", inventory.size());

            for (auto const& [obj, data] : inventory) {
                BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance scanning obj={} targetBase={}",
                              static_cast<const void*>(obj), static_cast<void*>(base));

                if (obj != base) {
                    continue;
                }

                auto const* entry = data.second.get();
                if (!entry) {
                    BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance entry null");
                    return;
                }

                if (!entry->extraLists) {
                    BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance entry has no extraLists");
                    return;
                }

                BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance found entry extraLists size={}",
                              entry->extraLists->size());

                for (auto* x : *entry->extraLists) {
                    BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance examining extra={}",
                                  static_cast<void*>(x));

                    if (!x) {
                        continue;
                    }

                    const char* disp = x->GetDisplayName(base);
                    if (!disp) {
                        BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance display name null");
                        continue;
                    }

                    BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance display='{}'", disp);

                    const std::size_t len = std::strlen(disp);
                    if (len >= kTagLen && std::memcmp(disp + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                        BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance found tagged instance extra={}",
                                      static_cast<void*>(x));
                        RemoveChosenTag(base, x);
                    }
                }

                BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance finished target base");
                return;
            }

            BOW_DEBUG_LOG("[ChosenBow] RemoveTagFromAnyTaggedInstance target base not found in inventory");
        }

        RE::InventoryEntryData* FindLiveInventoryEntryForBase(RE::TESBoundObject const* base) {
            BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase begin base={}", static_cast<const void*>(base));

            if (!base) {
                BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase early return: base null");
                return nullptr;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase early return: player null");
                return nullptr;
            }

            auto const* changes = player->GetInventoryChanges();
            BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase changes={}", static_cast<const void*>(changes));

            if (!changes || !changes->entryList) {
                BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase early return: changes or entryList null");
                return nullptr;
            }

            BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase entryList={} size={}",
                          static_cast<const void*>(changes->entryList), changes->entryList->size());

            for (auto* entry : *changes->entryList) {
                BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase scanning entry={} object={} extraLists={}",
                              static_cast<void*>(entry), entry ? static_cast<void*>(entry->object) : nullptr,
                              entry ? static_cast<const void*>(entry->extraLists) : nullptr);

                if (!entry || !entry->object) {
                    continue;
                }

                if (entry->object == base) {
                    BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase matched entry={}",
                                  static_cast<void*>(entry));
                    return entry;
                }
            }

            BOW_DEBUG_LOG("[ChosenBow] FindLiveInventoryEntryForBase no entry found for base={}",
                          static_cast<const void*>(base));
            return nullptr;
        }

        RE::ExtraDataList* EnsureInstanceExtraForBase(RE::TESBoundObject* base) {
            BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase begin base={}", static_cast<void*>(base));

            if (!base) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase early return: base null");
                return nullptr;
            }

            auto* entry = FindLiveInventoryEntryForBase(base);
            if (!entry) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase early return: entry null");
                return nullptr;
            }

            BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase entry={} extraLists={}", static_cast<void*>(entry),
                          static_cast<const void*>(entry->extraLists));

            if (entry->extraLists) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase existing extraLists size={}",
                              entry->extraLists->size());

                for (auto* extra : *entry->extraLists) {
                    BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase existing extra={}",
                                  static_cast<void*>(extra));

                    if (extra) {
                        BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase reusing extra={}",
                                      static_cast<void*>(extra));
                        return extra;
                    }
                }

                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase extraLists exists but all entries null");
            } else {
                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase entry has no extraLists yet");
            }

            auto* extra = new RE::ExtraDataList();
            if (!extra) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase failed to allocate ExtraDataList");
                return nullptr;
            }

            BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase allocated new extra={}", static_cast<void*>(extra));

            entry->AddExtraList(extra);

            BOW_DEBUG_LOG("[ChosenBow] EnsureInstanceExtraForBase after AddExtraList extraLists={} size={}",
                          static_cast<const void*>(entry->extraLists),
                          entry->extraLists ? entry->extraLists->size() : 0);

            return extra;
        }

        RE::ExtraUniqueID* GetExtraUniqueID(RE::ExtraDataList* extra) {
            BOW_DEBUG_LOG("[ChosenBow] GetExtraUniqueID extra={}", static_cast<void*>(extra));

            if (!extra) {
                BOW_DEBUG_LOG("[ChosenBow] GetExtraUniqueID early return: extra null");
                return nullptr;
            }

            auto* uid = extra->GetByType<RE::ExtraUniqueID>();
            BOW_DEBUG_LOG("[ChosenBow] GetExtraUniqueID result={}", static_cast<void*>(uid));
            return uid;
        }

        std::uint16_t ComputeNextUniqueID(RE::TESBoundObject const* base) {
            BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID begin base={}", static_cast<const void*>(base));

            if (!base) {
                BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID base null, returning 1");
                return 1;
            }

            auto* entry = FindLiveInventoryEntryForBase(base);
            if (!entry) {
                BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID no live entry, returning 1");
                return 1;
            }

            std::uint16_t maxID = 0;

            BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID live entry={} extraLists={}", static_cast<void*>(entry),
                          static_cast<const void*>(entry->extraLists));

            if (entry->extraLists) {
                for (auto* extra : *entry->extraLists) {
                    if (!extra) {
                        continue;
                    }

                    if (auto* xid = GetExtraUniqueID(extra)) {
                        BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID found uid={} on extra={}", xid->uniqueID,
                                      static_cast<void*>(extra));
                        if (xid->uniqueID > maxID) {
                            maxID = xid->uniqueID;
                        }
                    }
                }
            }

            if (maxID == std::numeric_limits<std::uint16_t>::max()) {
                BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID saturated maxID={}", maxID);
                return maxID;
            }

            BOW_DEBUG_LOG("[ChosenBow] ComputeNextUniqueID next={}", static_cast<std::uint16_t>(maxID + 1));
            return static_cast<std::uint16_t>(maxID + 1);
        }

        RE::ExtraUniqueID* EnsureExtraUniqueID(RE::TESBoundObject const* base, RE::ExtraDataList* extra) {
            BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID begin base={} extra={}", static_cast<const void*>(base),
                          static_cast<void*>(extra));

            if (!base || !extra) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID early return: base or extra null");
                return nullptr;
            }

            if (auto* existing = GetExtraUniqueID(extra)) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID reusing existing uid={}", existing->uniqueID);
                return existing;
            }

            const auto nextID = ComputeNextUniqueID(base);
            BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID computed nextID={}", nextID);

            auto* uid = RE::BSExtraData::Create<RE::ExtraUniqueID>();
            if (!uid) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID failed to allocate ExtraUniqueID");
                return nullptr;
            }

            uid->next = nullptr;
            uid->uniqueID = nextID;

            extra->Add(uid);

            BOW_DEBUG_LOG("[ChosenBow] EnsureExtraUniqueID added uid={} to extra={}", uid->uniqueID,
                          static_cast<void*>(extra));

            return uid;
        }
    }

    RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject const* base) {
        BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase begin base={}", static_cast<const void*>(base));

        if (!base) {
            BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase early return: base null");
            return nullptr;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase early return: player null");
            return nullptr;
        }

        auto inventory = player->GetInventory([base](RE::TESBoundObject const& obj) { return &obj == base; });
        BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase filtered inventory entries={}", inventory.size());

        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto const* entry = data.second.get();
            BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase entry={} extraLists={}",
                          static_cast<const void*>(entry),
                          entry ? static_cast<const void*>(entry->extraLists) : nullptr);

            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase candidate extra={}", static_cast<void*>(extra));
                if (extra) {
                    BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase returning extra={}",
                                  static_cast<void*>(extra));
                    return extra;
                }
            }
        }

        BOW_DEBUG_LOG("[ChosenBow] FindAnyInstanceExtraForBase none found");
        return nullptr;
    }

    RE::ExtraDataList* FindLiveExtraByUniqueID(RE::TESBoundObject const* base, std::uint16_t uniqueID) {
        BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID begin base={} uniqueID={}", static_cast<const void*>(base),
                      uniqueID);

        if (!base || uniqueID == 0) {
            BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID early return: base null or uniqueID zero");
            return nullptr;
        }

        auto const* entry = FindLiveInventoryEntryForBase(base);
        BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID live entry={}", static_cast<const void*>(entry));

        if (!entry || !entry->extraLists) {
            BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID early return: entry null or extraLists null");
            return nullptr;
        }

        BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID extraLists={} size={}",
                      static_cast<const void*>(entry->extraLists), entry->extraLists->size());

        for (auto* extra : *entry->extraLists) {
            BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID scanning extra={}", static_cast<void*>(extra));

            if (!extra) {
                continue;
            }

            auto const* xid = extra->GetByType<RE::ExtraUniqueID>();
            BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID extra={} xid={} xidValue={}", static_cast<void*>(extra),
                          static_cast<const void*>(xid), xid ? xid->uniqueID : 0);

            if (xid && xid->uniqueID == uniqueID) {
                BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID matched extra={} for uniqueID={}",
                              static_cast<void*>(extra), uniqueID);
                return extra;
            }
        }

        BOW_DEBUG_LOG("[ChosenBow] FindLiveExtraByUniqueID no extra found for uniqueID={}", uniqueID);
        return nullptr;
    }

    RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject const* base, RE::ExtraDataList const* candidate) {
        BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra begin base={} candidate={}", static_cast<const void*>(base),
                      static_cast<const void*>(candidate));

        if (!base || !candidate) {
            BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra early return: base or candidate null");
            return nullptr;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra early return: player null");
            return nullptr;
        }

        auto inventory = player->GetInventory();
        BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra inventory entries={}", inventory.size());

        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto const* entry = data.second.get();
            BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra entry={} extraLists={}", static_cast<const void*>(entry),
                          entry ? static_cast<const void*>(entry->extraLists) : nullptr);

            if (!entry || !entry->extraLists) {
                BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra entry missing extraLists");
                return nullptr;
            }

            for (auto* x : *entry->extraLists) {
                BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra comparing liveExtra={} candidate={}", static_cast<void*>(x),
                              static_cast<const void*>(candidate));
                if (x == candidate) {
                    BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra matched candidate");
                    return x;
                }
            }

            BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra base found but candidate not present");
            return nullptr;
        }

        BOW_DEBUG_LOG("[ChosenBow] ResolveLiveExtra base not found in inventory");
        return nullptr;
    }

    void LoadChosenBow(RE::TESObjectWEAP* bow, std::uint16_t uniqueID) {
        BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow begin bow={} uniqueID={}", static_cast<void*>(bow), uniqueID);

        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;

        if (!bow) {
            BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow early return: bow null");
            return;
        }

        auto* base = bow->As<RE::TESBoundObject>();
        if (!base) {
            BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow early return: base null");
            return;
        }

        st.chosenBow.base = base;

        if (uniqueID != 0) {
            st.chosenBow.extra = FindLiveExtraByUniqueID(base, uniqueID);
            BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow FindLiveExtraByUniqueID returned extra={}",
                          static_cast<void*>(st.chosenBow.extra));
        }

        bool usedFallback = false;
        if (!st.chosenBow.extra) {
            usedFallback = true;
            BOW_DEBUG_LOG(
                "[ChosenBow] LoadChosenBow uniqueID lookup failed, falling back to EnsureInstanceExtraForBase");
            st.chosenBow.extra = EnsureInstanceExtraForBase(base);
            BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow EnsureInstanceExtraForBase returned extra={}",
                          static_cast<void*>(st.chosenBow.extra));
        }

        std::uint16_t resolvedUniqueID = 0;
        if (st.chosenBow.base && st.chosenBow.extra) {
            if (auto const* xid = GetExtraUniqueID(st.chosenBow.extra)) {
                resolvedUniqueID = xid->uniqueID;
                BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow existing uniqueID={}", resolvedUniqueID);
            } else if (auto const* xidd = EnsureExtraUniqueID(st.chosenBow.base, st.chosenBow.extra)) {
                resolvedUniqueID = xidd->uniqueID;
                BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow ensured uniqueID={}", resolvedUniqueID);
            }
        }

        cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
        cfg.chosenBowUniqueID.store(resolvedUniqueID, std::memory_order_relaxed);
        cfg.Save();

        RemoveTagFromAnyTaggedInstance(base);

        BOW_DEBUG_LOG("[ChosenBow] LoadChosenBow end base={} extra={} resolvedUniqueID={} usedFallback={}",
                      static_cast<void*>(st.chosenBow.base), static_cast<void*>(st.chosenBow.extra), resolvedUniqueID,
                      usedFallback);
    }

    void ClearChosenBow() {
        BOW_DEBUG_LOG("[ChosenBow] ClearChosenBow begin");

        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        BOW_DEBUG_LOG("[ChosenBow] ClearChosenBow current base={} extra={}", static_cast<void*>(st.chosenBow.base),
                      static_cast<void*>(st.chosenBow.extra));

        if (st.chosenBow.base && st.chosenBow.extra) {
            RemoveChosenTag(st.chosenBow.base, st.chosenBow.extra);
        }

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        cfg.chosenBowUniqueID.store(0u, std::memory_order_relaxed);
        cfg.Save();

        BOW_DEBUG_LOG("[ChosenBow] ClearChosenBow end");
    }

    bool EnsureChosenBowInInventory() {
        BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory begin");

        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (!player) {
            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory early return: player null");
            return false;
        }

        BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory current base={} extra={}",
                      static_cast<void*>(st.chosenBow.base), static_cast<void*>(st.chosenBow.extra));

        if (!st.chosenBow.base) {
            const auto id = cfg.chosenBowFormID.load(std::memory_order_relaxed);
            const auto uniqueID = cfg.chosenBowUniqueID.load(std::memory_order_relaxed);
            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory no base in state, cfg formID={:08X}", id);

            if (id != 0) {
                if (auto* form = RE::TESForm::LookupByID<RE::TESObjectWEAP>(id)) {
                    BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory loading bow from config form={}",
                                  static_cast<void*>(form));
                    LoadChosenBow(form, uniqueID);
                } else {
                    BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory LookupByID failed");
                }
            }

            if (!st.chosenBow.base) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory still no base after config load");
                return false;
            }
        }

        auto const* chosenBase = st.chosenBow.base;
        auto inventory =
            player->GetInventory([chosenBase](RE::TESBoundObject const& obj) { return &obj == chosenBase; });

        BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory filtered inventory entries={}", inventory.size());

        bool anyFound = false;
        RE::ExtraDataList* foundAnyExtra = nullptr;
        RE::ExtraDataList const* foundExact = nullptr;

        for (auto const& [obj, data] : inventory) {
            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory loop obj={} count={}", static_cast<const void*>(obj),
                          data.first);

            if (obj != chosenBase) {
                continue;
            }

            if (data.first > 0) {
                anyFound = true;
            }

            auto const* entry = data.second.get();
            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory entry={} extraLists={}",
                          static_cast<const void*>(entry),
                          entry ? static_cast<const void*>(entry->extraLists) : nullptr);

            if (!entry || !entry->extraLists || entry->extraLists->empty()) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory entry has no usable extraLists");
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory checking extra={}", static_cast<void*>(extra));

                if (!extra) {
                    continue;
                }

                if (!foundAnyExtra) {
                    foundAnyExtra = extra;
                    BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory foundAnyExtra set to {}",
                                  static_cast<void*>(foundAnyExtra));
                }

                const char* disp = extra->GetDisplayName(obj);
                if (!disp || !*disp) {
                    BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory display name missing");
                    continue;
                }

                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory display='{}'", disp);

                if (st.chosenBow.extra && extra == st.chosenBow.extra) {
                    BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory found exact extra match");
                    foundExact = extra;
                    break;
                }
            }

            if (foundExact) {
                break;
            }
        }

        BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory summary anyFound={} foundAnyExtra={} foundExact={}",
                      anyFound, static_cast<void*>(foundAnyExtra), static_cast<const void*>(foundExact));

        if (foundExact) {
            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory returning true via foundExact");
            return true;
        }

        if (foundAnyExtra) {
            st.chosenBow.extra = foundAnyExtra;

            std::uint16_t uniqueID = 0;
            if (auto const* xid = GetExtraUniqueID(foundAnyExtra)) {
                uniqueID = xid->uniqueID;
            } else if (auto const* xidd = EnsureExtraUniqueID(st.chosenBow.base, foundAnyExtra)) {
                uniqueID = xidd->uniqueID;
            }

            const auto formID = st.chosenBow.base->As<RE::TESObjectWEAP>()
                                    ? st.chosenBow.base->As<RE::TESObjectWEAP>()->GetFormID()
                                    : 0u;

            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory using foundAnyExtra={} saving formID={:08X}",
                          static_cast<void*>(foundAnyExtra), formID);

            cfg.chosenBowFormID.store(formID, std::memory_order_relaxed);
            cfg.chosenBowUniqueID.store(uniqueID, std::memory_order_relaxed);
            cfg.Save();
            return true;
        }

        if (anyFound) {
            BOW_DEBUG_LOG(
                "[ChosenBow] EnsureChosenBowInInventory base found but no extra, attempting "
                "EnsureInstanceExtraForBase");

            st.chosenBow.extra = EnsureInstanceExtraForBase(st.chosenBow.base);

            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory EnsureInstanceExtraForBase returned extra={}",
                          static_cast<void*>(st.chosenBow.extra));

            std::uint16_t uniqueID = 0;
            if (st.chosenBow.base && st.chosenBow.extra) {
                if (auto const* xid = EnsureExtraUniqueID(st.chosenBow.base, st.chosenBow.extra)) {
                    uniqueID = xid->uniqueID;
                }
                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory EnsureExtraUniqueID returned uid={}", uniqueID);
            }

            if (auto const* bow = st.chosenBow.base->As<RE::TESObjectWEAP>()) {
                BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory saving bow formID={:08X}, uniqueID={}",
                              bow->GetFormID(), uniqueID);
                cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
                cfg.chosenBowUniqueID.store(uniqueID, std::memory_order_relaxed);
                cfg.Save();
            }

            BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory returning {} after anyFound path",
                          st.chosenBow.extra != nullptr);

            return st.chosenBow.extra != nullptr;
        }

        BOW_DEBUG_LOG("[ChosenBow] EnsureChosenBowInInventory chosen bow not found, clearing");
        ClearChosenBow();
        return false;
    }

    void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra) {
        BOW_DEBUG_LOG("[ChosenBow] SetChosenBow begin bow={} extra={}", static_cast<void*>(bow),
                      static_cast<void*>(extra));

        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        const auto newBase = bow ? bow->As<RE::TESBoundObject>() : nullptr;

        BOW_DEBUG_LOG("[ChosenBow] SetChosenBow current base={} extra={} newBase={}",
                      static_cast<void*>(st.chosenBow.base), static_cast<void*>(st.chosenBow.extra),
                      static_cast<const void*>(newBase));

        if (st.chosenBow.base) {
            BOW_DEBUG_LOG("[ChosenBow] SetChosenBow removing existing tags from old base={}",
                          static_cast<void*>(st.chosenBow.base));
            RemoveTagFromAnyTaggedInstance(st.chosenBow.base);
        }

        if (st.chosenBow.base == newBase && st.chosenBow.extra == extra) {
            BOW_DEBUG_LOG("[ChosenBow] SetChosenBow same selection detected, toggling off");

            st.chosenBow.base = nullptr;
            st.chosenBow.extra = nullptr;
            cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
            cfg.chosenBowUniqueID.store(0u, std::memory_order_relaxed);
            cfg.Save();
            return;
        }

        st.chosenBow.base = newBase;
        st.chosenBow.extra = extra;

        BOW_DEBUG_LOG("[ChosenBow] SetChosenBow assigned base={} extra={}", static_cast<void*>(st.chosenBow.base),
                      static_cast<void*>(st.chosenBow.extra));

        if (st.chosenBow.base && !st.chosenBow.extra) {
            BOW_DEBUG_LOG("[ChosenBow] SetChosenBow no extra supplied, calling EnsureInstanceExtraForBase");
            st.chosenBow.extra = EnsureInstanceExtraForBase(st.chosenBow.base);
            BOW_DEBUG_LOG("[ChosenBow] SetChosenBow EnsureInstanceExtraForBase returned extra={}",
                          static_cast<void*>(st.chosenBow.extra));
        }

        std::uint16_t chosenUniqueID = 0;
        if (st.chosenBow.base && st.chosenBow.extra) {
            if (auto const* uid = EnsureExtraUniqueID(st.chosenBow.base, st.chosenBow.extra)) {
                chosenUniqueID = uid->uniqueID;
            }
            BOW_DEBUG_LOG("[ChosenBow] SetChosenBow EnsureExtraUniqueID returned uid={}", chosenUniqueID);
        }

        const auto formID = bow ? bow->GetFormID() : 0u;
        BOW_DEBUG_LOG("[ChosenBow] SetChosenBow saving chosenBowFormID={:08X}", formID);

        cfg.chosenBowFormID.store(formID, std::memory_order_relaxed);
        cfg.chosenBowUniqueID.store(chosenUniqueID, std::memory_order_relaxed);
        cfg.Save();

        BOW_DEBUG_LOG("[ChosenBow] SetChosenBow end state base={} extra={}", static_cast<void*>(st.chosenBow.base),
                      static_cast<void*>(st.chosenBow.extra));
    }

    RE::TESAmmo* GetPreferredArrow() {
        const auto id = IntegratedBow::GetBowConfig().preferredArrowFormID.load(std::memory_order_relaxed);
        BOW_DEBUG_LOG("[ChosenBow] GetPreferredArrow formID={:08X}", id);

        auto* ammo = id ? RE::TESForm::LookupByID<RE::TESAmmo>(id) : nullptr;

        BOW_DEBUG_LOG("[ChosenBow] GetPreferredArrow result={}", static_cast<void*>(ammo));
        return ammo;
    }

    void SetPreferredArrow(RE::TESAmmo const* ammo) {
        auto& cfg = IntegratedBow::GetBowConfig();
        const auto formID = ammo ? ammo->GetFormID() : 0u;

        BOW_DEBUG_LOG("[ChosenBow] SetPreferredArrow ammo={} formID={:08X}", static_cast<const void*>(ammo), formID);

        cfg.preferredArrowFormID.store(formID, std::memory_order_relaxed);
        cfg.Save();

        BOW_DEBUG_LOG("[ChosenBow] SetPreferredArrow saved");
    }
}