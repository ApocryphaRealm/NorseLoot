// NorseLoot - rewards, shops and stashes by rule (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-17.
// Rules add or remove entries in lists nested anywhere in loaded objects - a trader's stock (TraderProfile
// Tiers[*].ItemPools[*].Items), a spawn point's loot (LootItems) - by name, folder, field content and chance, so several
// packs add to the same list without overwriting it. Modelled on Container Item Distributor (Skyrim). Survey:
// RESEARCH-more-frameworks.md (#13).
//
// loot\*.ini (NorseManifest order). A section is the class; its List names where the entries live:
//   [TraderProfile]
//   List = Tiers[*].ItemPools[*].Items
//   Load = /Game/Norse/Core/Data/Worksites/TraderProfiles/TravelingMerchants/Merchant_Profile_Boat_01.Merchant_Profile_Boat_01
//   Add = (Item="/Game/.../Tier1_Common_Axe_01.Tier1_Common_Axe_01",SuperiorChance=10,MaxQuantity=2) | names | folders | fields | chance
//   Remove = Tier1_Common_Axe_01 | Merchant_Profile_Boat_*
// Add skips a list that already has an entry with the value's first quoted path (rules are safe to run again);
// Remove drops every entry whose text contains the given text. Applied at each map load; console nl.reload, nl.apply,
// nl.dump <object path>.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"

#include <map>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod, g_out;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    struct Edit
    {
        bool add = true;
        std::wstring value;
        Filters f;
        std::string where;
    };
    struct Section
    {
        std::wstring cls, list;
        std::vector<std::wstring> loads;
        std::vector<Edit> edits;
        std::string where;
    };
    std::vector<Section> g_sections;

    void LoadRules()
    {
        g_sections.clear();
        fs::path dir = g_mod / "loot";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        size_t edits = 0;
        for (const auto& f : files)
        {
            Section* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_sections.push_back({Trim(l.substr(1, l.size() - 2)), L"", {}, {}, where});
                    cur = &g_sections.back();
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [Class] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"list") cur->list = v;
                else if (k == L"load") cur->loads.push_back(v);
                else if (k == L"add" || k == L"remove")
                {
                    auto parts = SplitList(v, L'|');
                    Edit e;
                    e.add = k == L"add";
                    e.value = parts[0];
                    e.where = where;
                    if (parts.size() > 1) e.f.names = ParseFilter(parts[1]);
                    if (parts.size() > 2) e.f.folders = ParseFilter(parts[2]);
                    if (parts.size() > 3) e.f.fields = ParseFields(parts[3], where);
                    if (parts.size() > 4) e.f.chance = ParseChance(parts[4]);
                    if (e.value.empty()) NRN_WARN("%s: no value - skipped", where.c_str());
                    else
                    {
                        cur->edits.push_back(e);
                        ++edits;
                    }
                }
                else NRN_WARN("%s: unknown key '%s' (List, Load, Add, Remove)", where.c_str(), Narrow(k).c_str());
            }
        }
        g_sections.erase(std::remove_if(g_sections.begin(), g_sections.end(), [](const Section& s) {
                             if (s.list.empty()) NRN_WARN("[%s] (%s) has no List = line - left out", Narrow(s.cls).c_str(), s.where.c_str());
                             return s.list.empty();
                         }),
                         g_sections.end());
        NRN_INFO("loot rules: %zu section(s), %zu edit(s) from %zu file(s)", g_sections.size(), edits, files.size());
    }

    // the value's first quoted text: the asset an entry is about, used to tell whether a list already has it
    std::wstring KeyOf(const std::wstring& v)
    {
        size_t a = v.find(L'"');
        size_t b = a == std::wstring::npos ? a : v.find(L'"', a + 1);
        return Lower(b == std::wstring::npos ? v : v.substr(a + 1, b - a - 1));
    }

    UObject* Load(const std::wstring& path);

    void ApplySection(const Section& s, int& changed, int& lists)
    {
        for (const auto& l : s.loads)
            if (!Load(l)) NRN_WARN("[%s] Load %s: not found", Narrow(s.cls).c_str(), Narrow(l).c_str());
        std::vector<UObject*> objs;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(s.cls.c_str(), objs);
        }
        catch (...)
        {
        }
        for (UObject* obj : objs)
        {
            std::wstring full = obj->GetFullName(nullptr);
            std::wstring path = full.substr(full.find(L' ') + 1), name = ShortName(path);
            if (name.rfind(L"Default__", 0) == 0) continue;
            auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(obj));
            for (const ArrayAt& arr : ResolveArrays(obj, s.list))
            {
                ++lists;
                std::wstring before = Export(arr.prop, arr.addr, obj);
                auto els = ListElements(before);
                size_t start = els.size();
                std::string done;
                for (const Edit& e : s.edits)
                {
                    if (!Rejects(e.f, obj, cls, name, path, name + L"|" + e.value).empty()) continue;
                    if (e.add)
                    {
                        std::wstring key = KeyOf(e.value);
                        if (std::any_of(els.begin(), els.end(), [&](const std::wstring& x) { return Lower(x).find(key) != std::wstring::npos; })) continue;
                        // keep the added item loaded: the list's owner may be a start-up asset whose references do not
                        // keep it alive (NorseSound 0.1.0 found that with a swapped-in sound)
                        if (key.rfind(L"/", 0) == 0) LoadKept(key.c_str());
                        els.push_back(e.value);
                        done += " +" + Narrow(ShortName(key));
                    }
                    else
                    {
                        std::wstring t = Lower(e.value);
                        size_t n0 = els.size();
                        els.erase(std::remove_if(els.begin(), els.end(), [&](const std::wstring& x) { return Lower(x).find(t) != std::wstring::npos; }),
                                  els.end());
                        if (els.size() != n0) done += " -" + Narrow(e.value) + "(x" + std::to_string(n0 - els.size()) + ")";
                    }
                }
                if (done.empty()) continue;
                std::wstring text = L"(";
                for (size_t i = 0; i < els.size(); ++i) text += (i ? L"," : L"") + els[i];
                text += L")";
                bool faulted = false;
                const wchar_t* ok = SehImport(arr.prop, text.c_str(), arr.addr, obj, &faulted);
                size_t now = ListElements(Export(arr.prop, arr.addr, obj)).size();
                if (!ok || faulted || now != els.size())
                {
                    NRN_WARN("%s %s: Unreal did not accept the list (%zu entries expected, %zu now)%s", Narrow(name).c_str(), arr.label.c_str(), els.size(),
                             now, faulted ? " (fault caught)" : "");
                    continue;
                }
                ++changed;
                RecordWrite(obj, arr.prop, arr.addr, text, name + L" " + Widen(arr.label));
                NRN_INFO("%s %s:%s (%zu -> %zu entries)", Narrow(name).c_str(), arr.label.c_str(), done.c_str(), start, now);
            }
        }
    }

    UObject* Load(const std::wstring& path)
    {
        try
        {
            if (UObject* o = RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, path.c_str(), false)) return o;
        }
        catch (...)
        {
        }
        static UObject* ksl = RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, L"/Script/Engine.Default__KismetSystemLibrary", false);
        if (!ksl) return nullptr;
        struct FStr
        {
            const wchar_t* data;
            int32_t num, max;
        } str{path.c_str(), static_cast<int32_t>(path.size() + 1), static_cast<int32_t>(path.size() + 1)};
        uint16_t ps1 = Reflect::Get().ParmsSizeOf(ksl, L"MakeSoftObjectPath"), ps2 = Reflect::Get().ParmsSizeOf(ksl, L"Conv_SoftObjPathToSoftObjRef"),
                 ps3 = Reflect::Get().ParmsSizeOf(ksl, L"LoadAsset_Blocking");
        if (!ps1 || !ps2 || !ps3 || ps1 > 200 || ps2 > 200 || ps3 > 200) return nullptr;
        Buf a;
        std::memcpy(a.b, &str, sizeof(str));
        if (!Reflect::Get().Call(ksl, L"MakeSoftObjectPath", a.b, ps1, sizeof(a.b))) return nullptr;
        size_t pathSize = ps1 - 0x10;
        Buf b;
        std::memcpy(b.b, a.b + 0x10, pathSize);
        if (!Reflect::Get().Call(ksl, L"Conv_SoftObjPathToSoftObjRef", b.b, ps2, sizeof(b.b))) return nullptr;
        size_t retOff = (pathSize + 7) & ~size_t(7);
        Buf c;
        std::memcpy(c.b, b.b + retOff, ps2 - retOff);
        if (!Reflect::Get().Call(ksl, L"LoadAsset_Blocking", c.b, ps3, sizeof(c.b))) return nullptr;
        UObject* out = nullptr;
        std::memcpy(&out, c.b + (ps3 - 8), 8);
        return out;
    }

    void ApplyAll(const char* why)
    {
        int changed = 0, lists = 0;
        for (const Section& s : g_sections)
        {
            try
            {
                ApplySection(s, changed, lists);
            }
            catch (...)
            {
                NRN_ERROR("[%s] threw - skipped", Narrow(s.cls).c_str());
            }
        }
        NRN_INFO("loot (%s): %d list(s) changed of %d looked at", why, changed, lists);
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"nl.reload")
        {
            LoadRules();
            ApplyAll("nl.reload");
        }
        else if (cmd == L"nl.apply") ApplyAll("nl.apply");
        else if (cmd == L"nl.dump" && !arg.empty())
        {
            Load(arg);
            Dump(arg, g_out);
        }
        else NRN_WARN("unknown command - nl.reload | nl.apply | nl.dump <object path>");
        return true;
    }
} // namespace

class NorseLoot : public RC::CppUserModBase
{
  public:
    NorseLoot()
    {
        ModName = L"NorseLoot";
        ModVersion = L"" NL_VERSION;
        ModDescription = L"Rewards, shops and stashes by rule: add or remove entries in traders' stock and other item lists.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        int logLevel = 2; // uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 6 off
        for (const auto& raw : ReadLines(g_mod / "NorseLoot.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
            if (l.rfind(L"uLogLevel=", 0) == 0) logLevel = _wtoi(l.c_str() + 10);
        }
        g_out = dir.empty() ? g_mod : fs::path(dir);
        Log::Get().Open(g_out / "NorseLoot.log", logLevel);
        Log::Get().Write(Level::Error, "NorseLoot v%s - log level %d", NL_VERSION, logLevel);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                try
                {
                    RecheckWrites("map load"); // before the pass: it exports first, and a freed pointer would fault
                    ApplyAll("map load");
                }
                catch (...)
                {
                    NRN_ERROR("map-load loot pass threw");
                }
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"nl.", 3) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseLoot();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
