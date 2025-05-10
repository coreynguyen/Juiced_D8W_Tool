/*--------------------------------------------------------------
   main.cpp — dual-mode launcher for d8wTool
   -------------------------------------------------------------
   • CLI verbs (-export, -convert, -import …) run when argv[1]
     begins with '-'.
   • If no flag is present, we start the wxWidgets GUI.
--------------------------------------------------------------*/

#include <iostream>
#include <cstring>
#include <wx/wx.h>

#include "resource.h"
#include "d8wTool.h"        // GUI classes (d8wToolApp, MainFrame)
#include "d8w_parser.h"     // Parser for CLI jobs

/*--------------------------------------------------------------
   Register wxWidgets application class WITHOUT auto-generated
   main(), because we supply our own dual-mode main() below.
--------------------------------------------------------------*/
wxIMPLEMENT_APP_NO_MAIN(d8wToolApp);

/*──────────── CLI utilities ─────────────────────────────────*/
static void printUsage()
{
    std::cout <<
    "d8wTool CLI usage:\n"
    "  -export       <d8w> <pack> <idx> <out.ddt>\n"
    "  -exportset    <d8w> <pack> <outDir>\n"
    "  -convert      <d8w> <pack> <idx> <out.dds>\n"
    "  -convertset   <d8w> <pack> <outDir>\n"
    "  -import       <d8w> <pack> <idx> <in.ddt>\n"
    "  -importset    <d8w> <pack> <inDir>\n";
}

static bool parseUint(const char* s, size_t& out)
{
    char* end = 0;
    long v = strtol(s, &end, 10);
    if (!*s || *end || v < 0) return false;
    out = static_cast<size_t>(v);
    return true;
}

/* derive <stem>.d8t next to a .d8w file */
static std::string sisterD8T(const std::string& wPath)
{
    std::string t = wPath;
    size_t dot = t.find_last_of('.');
    if (dot != std::string::npos) t.erase(dot);
    t += ".d8t";
    return t;
}

/*──────────── CLI entrypoint  ───────────────────────────────*/
static int runCLI(int argc, char** argv)
{
    if (argc < 2) { printUsage(); return 1; }

    std::string verb = argv[1];
    auto bail = [](const char* m){ std::cout << m << '\n'; return 3; };

    juiced::D8WFile bank;

    /*---------------- export one ----------------*/
    if (verb == "-export" && argc == 6)
    {
        size_t pack, idx;
        if (!parseUint(argv[3], pack) || !parseUint(argv[4], idx))
            return printUsage(), 1;
        if (!bank.load(argv[2]))                   return bail("load failed");
        if (!bank.exportTexture(pack, idx, argv[5])) return bail("export failed");
        return 0;
    }
    /*---------------- export set ---------------*/
    else if (verb == "-exportset" && argc == 5)
    {
        size_t pack;
        if (!parseUint(argv[3], pack))             return printUsage(), 1;
        if (!bank.load(argv[2]))                   return bail("load failed");
        if (!bank.exportTextureSet(pack, argv[4])) return bail("exportset failed");
        return 0;
    }
    /*---------------- convert one --------------*/
    else if (verb == "-convert" && argc == 6)
    {
        size_t pack, idx;
        if (!parseUint(argv[3], pack) || !parseUint(argv[4], idx))
            return printUsage(), 1;
        if (!bank.load(argv[2]))                    return bail("load failed");
        if (!bank.convertTexture(pack, idx, argv[5])) return bail("convert failed");
        return 0;
    }
    /*---------------- convert set --------------*/
    else if (verb == "-convertset" && argc == 5)
    {
        size_t pack;
        if (!parseUint(argv[3], pack))              return printUsage(), 1;
        if (!bank.load(argv[2]))                    return bail("load failed");
        if (!bank.convertTextureSet(pack, argv[4])) return bail("convertset failed");
        return 0;
    }
    /*---------------- import one ---------------*/
    else if (verb == "-import" && argc == 6)
    {
        size_t pack, idx;
        if (!parseUint(argv[3], pack) || !parseUint(argv[4], idx))
            return printUsage(), 1;
        if (!bank.load(argv[2]))                    return bail("load failed");
        if (!bank.importTexture(pack, idx, argv[5])) return bail("import failed");
        if (bank.isDirty())
            bank.save(argv[2], sisterD8T(argv[2]));
        return 0;
    }
    /*---------------- import set ---------------*/
    else if (verb == "-importset" && argc == 5)
    {
        size_t pack;
        if (!parseUint(argv[3], pack))              return printUsage(), 1;
        if (!bank.load(argv[2]))                    return bail("load failed");
        if (!bank.importTextureSet(pack, argv[4]))  return bail("importset failed");
        if (bank.isDirty())
            bank.save(argv[2], sisterD8T(argv[2]));
        return 0;
    }

    printUsage();
    return 1;
}

/*──────────── program entrypoint  ───────────────────────────*/
int main(int argc, char** argv)
{
    /* 1) CLI mode when first arg is a flag */
    if (argc > 1 && argv[1][0] == '-')
        return runCLI(argc, argv);

    /* 2) GUI mode – start wxWidgets in the usual way */
    return wxEntry(argc, argv);    // this builds d8wToolApp & shows MainFrame
}
