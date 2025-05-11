/*───────────────────────────────────────────────────────────────┐
│  d8w_parser.cpp                                                │
│  (c) 2025 — Corey Van Nguyen · ChatGPT “Sophie”                │
└───────────────────────────────────────────────────────────────*/

#include "d8w_parser.h"

#include <windows.h>
#include <direct.h>      /* _mkdir                */
#include <algorithm>     /* std::min, std::sort   */
#include <cstring>       /* memcpy, memset        */

/* ─── DDS flags (copied from ddraw.h to keep the file self-contained) ─── */
#define DDS_MAGIC           0x20534444      /* "DDS "                       */
#define DDSD_CAPS           0x00000001
#define DDSD_HEIGHT         0x00000002
#define DDSD_WIDTH          0x00000004
#define DDSD_PIXELFORMAT    0x00001000
#define DDSD_MIPMAPCOUNT    0x00020000
#define DDSD_LINEARSIZE     0x00080000
#define DDPF_ALPHAPIXELS    0x00000001
#define DDPF_FOURCC         0x00000004
#define DDPF_RGB            0x00000040
#define DDSCAPS_TEXTURE     0x00001000
#define DDSCAPS_COMPLEX     0x00000008
#define DDSCAPS_MIPMAP      0x00400000

/* ────────────────────────────────────────────────────────────── */
/*  little-endian helpers (local, header keeps only templates)    */
/* ────────────────────────────────────────────────────────────── */
namespace
{
    template<typename T>
    inline T rd(const BYTE*& p)            /* read   LE, advance ptr */
    {
        T v; std::memcpy(&v, p, sizeof(T)); p += sizeof(T);
        return v;
    }
    template<typename T>
    inline void wr(BYTE*& p, T v)          /* write  LE, advance ptr */
    {
        std::memcpy(p, &v, sizeof(T)); p += sizeof(T);
    }
    inline void pushLE32(std::vector<BYTE>& v, uint32_t x)  /* append */
    {
        v.push_back((BYTE)( x        & 0xFF));
        v.push_back((BYTE)((x >>  8) & 0xFF));
        v.push_back((BYTE)((x >> 16) & 0xFF));
        v.push_back((BYTE)((x >> 24) & 0xFF));
    }

    /* quick & dirty file-to-memory */
    static bool fileToMem(const std::string& path, std::vector<BYTE>& dst)
    {
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER sz;
        if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }

        dst.resize((size_t)sz.QuadPart);
        DWORD br;
        BOOL ok = ReadFile(h, &dst[0], (DWORD)dst.size(), &br, NULL);
        CloseHandle(h);
        return ok && br == dst.size();
    }

    /* mkdir if needed (returns TRUE if exists on exit) */
    inline bool ensureDir(const std::string& d)
    {
        DWORD attr = GetFileAttributesA(d.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES) || (_mkdir(d.c_str()) == 0);
    }
} /* anonymous namespace */

/*───────────────────────────────────────────────────────────────*/
/*  juiced::D8TFile                                              */
/*───────────────────────────────────────────────────────────────*/
using namespace juiced;

bool D8TFile::loadFileToMem(const std::string& p,
                            std::vector<BYTE>& dst) const
{
    return fileToMem(p, dst);
}

bool D8TFile::load(const std::string& p)
{
    pathT_ = p;
    return loadFileToMem(p, buf_);
}

/*───────────────────────────────────────────────────────────────*/
/*  juiced::D8WBank — ctor/dtor                                  */
/*───────────────────────────────────────────────────────────────*/
D8WBank::D8WBank() : dirty_(false), tBuf_(NULL) {}
D8WBank::~D8WBank() {}

/*───────────────────────────────────────────────────────────────*/
/*  helper: locate companion .d8t                                */
/*───────────────────────────────────────────────────────────────*/
bool D8WBank::locateD8T(const std::string& folder,
                        const std::string& stem,
                        const std::string& hint,
                        std::string& out) const
{
    /* explicit hint wins */
    if (!hint.empty()) { out = hint; return true; }

    /* same stem? */
    out = folder + "\\" + stem + ".d8t";
    if (GetFileAttributesA(out.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    /* fallback: first .d8t in folder */
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((folder + "\\*.d8t").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        out = folder + "\\" + fd.cFileName;
        FindClose(h);
        return true;
    }
    return false;
}

/*───────────────────────────────────────────────────────────────*/
/*  load() — parse one .d8w, borrow shared .d8t buffer           */
/*───────────────────────────────────────────────────────────────*/
bool D8WBank::loadFileToMem(const std::string& p,
                            std::vector<BYTE>& dst) const
{
    return fileToMem(p, dst);
}

bool D8WBank::load(const std::string& wPath,
                   const std::vector<BYTE>& sharedTbuf)
{
    /* keep pointer to big texture buffer owned by D8TFile */
    tBuf_  = const_cast< std::vector<BYTE>* >(&sharedTbuf);
    pathW_ = wPath;

    /* figure out folder / stem for implicit .d8t search */
    size_t slash = wPath.find_last_of("\\/");
    if (slash == std::string::npos) slash = 0;
    size_t dot   = wPath.find_last_of('.');
    if (dot   == std::string::npos) dot   = wPath.size();

    std::string folder = wPath.substr(0, slash);
    std::string stem   = wPath.substr(slash ? (slash + 1) : 0,
                                      dot - slash - 1);

    locateD8T(folder, stem, "", pathT_);   /* ignore result: UI may show "" */

    /* read .d8w to memory */
    if (!fileToMem(wPath, wBuf_)) return false;

    /* begin parsing --------------------------------------------------- */
    const BYTE* p   = &wBuf_[0];
    const BYTE* end = p + wBuf_.size();
    if ((size_t)(end - p) < 12) return false;

    uint32_t totalTex = rd<uint32_t>(p);
    uint32_t tblCnt   = rd<uint32_t>(p);
    uint32_t totalSz  = rd<uint32_t>(p);

    texBuf_.resize(tblCnt);

    uint32_t cursor = 0;        /* absolute cursor in .d8t stream */

    for (size_t i = 0; i < tblCnt; ++i)
    {
        if ((size_t)(end - p) < 12) return false;

        TextureTable& tbl = texBuf_[i];
        tbl.skip = rd<uint32_t>(p);      /* bytes to skip BEFORE pack */
        tbl.size = rd<uint32_t>(p);      /* bytes occupied by pack   */
        uint32_t n = rd<uint32_t>(p);    /* #textures */

        if ((size_t)(end - p) < n * sizeof(TextureHdr)) return false;
        tbl.tex.resize(n);

        /* copy raw headers as-is */
        size_t j;
        for (j = 0; j < n; ++j)
        {
            std::memcpy(&tbl.tex[j], p, sizeof(TextureHdr));
            p += sizeof(TextureHdr);
            tbl.tex[j].modified = false;      /* init */
        }

        /* compute absolute offsets for each texture ------------------ */
        cursor   += tbl.skip;                 /* align - skip first  */
        tbl.absOff = cursor;                  /* store for curiosity */

        uint32_t off = cursor;
        for (j = 0; j < n; ++j)
        {
            tbl.tex[j].fileOff = off;
            off += tbl.tex[j].size;
        }
        cursor = tbl.absOff + tbl.size;       /* jump to next pack   */
    }

    /* texture-set section (names + index tables) --------------------- */
    if ((size_t)(end - p) < 4) return false;
    uint32_t setCnt = rd<uint32_t>(p);
    texSet_.resize(setCnt);

    if (setCnt)
    {
        const BYTE* startSets = p;

        /* heuristic to find stride ---------------------------------- */
        if ((size_t)(end - p) < 32) return false;
        uint32_t firstName = rd<uint32_t>(p);
        p += 28;

        uint32_t stride = 0;
        const BYTE* probe;
        for (probe = p; probe + 4 <= end; )
        {
            if (rd<uint32_t>(probe) == firstName)
            {
                stride = (uint32_t)((probe - startSets - 36) / 4);
                break;
            }
        }
        if (stride == 0 && setCnt > 1) return false;

        p = startSets;
        for (size_t s = 0; s < setCnt; ++s)
        {
            char nm[33] = {0};
            std::memcpy(nm, p, 32); p += 32;
            texSet_[s].name = nm;

            texSet_[s].indexTable.resize(stride);
            size_t k;
            for (k = 0; k < stride; ++k)
                texSet_[s].indexTable[k] = rd<int32_t>(p);
        }
    }

    /* remaining bytes → unknown tail                                */
    tailRaw_.assign(p, end);
    dirty_ = false;
    return true;
}

/*───────────────────────────────────────────────────────────────*/
/*  save() — write modified .d8w & .d8t                          */
/*───────────────────────────────────────────────────────────────*/
bool D8WBank::save(const std::string& outW, const std::string& outT)
{
    if (!dirty_ || !tBuf_) return false;

    /* ------------ rebuild .d8w header & texture tables ------------ */
    std::vector<BYTE> wOut;
    wOut.reserve(wBuf_.size());

    uint32_t totalTex = 0, totalSz = 0;
    size_t i;
    for (i = 0; i < texBuf_.size(); ++i)
    {
        totalTex += (uint32_t)texBuf_[i].tex.size();
        totalSz  += texBuf_[i].size;
    }
    pushLE32(wOut, totalTex);
    pushLE32(wOut, (uint32_t)texBuf_.size());
    pushLE32(wOut, totalSz);

    for (i = 0; i < texBuf_.size(); ++i)
    {
        const TextureTable& tbl = texBuf_[i];
        pushLE32(wOut, tbl.skip);
        pushLE32(wOut, tbl.size);
        pushLE32(wOut, (uint32_t)tbl.tex.size());

        size_t j;
        for (j = 0; j < tbl.tex.size(); ++j)
        {
            const TextureHdr* h = (const TextureHdr*)&tbl.tex[j];
            const BYTE* src = (const BYTE*)h;
            wOut.insert(wOut.end(), src, src + sizeof(TextureHdr));
        }
    }

    /* unknown tail (unchanged) ------------------------------------ */
    wOut.insert(wOut.end(), tailRaw_.begin(), tailRaw_.end());

    /* ------------ write both files -------------------------------- */
    const std::vector<BYTE>& big = *tBuf_;

    /* small lambda (C++03 style) */
    struct Saver {
        static bool save(const std::string& p,const std::vector<BYTE>& b)
        {
            HANDLE h = CreateFileA(p.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD bw;
            BOOL ok = WriteFile(h, &b[0], (DWORD)b.size(), &bw, NULL);
            CloseHandle(h);
            return ok && bw == b.size();
        }
    };

    if (!Saver::save(outW, wOut)) return false;
    if (!Saver::save(outT, big )) return false;

    /* reset modified flags */
    for (i = 0; i < texBuf_.size(); ++i)
    {
        size_t j;
        for (j = 0; j < texBuf_[i].tex.size(); ++j)
            texBuf_[i].tex[j].modified = false;
    }
    dirty_ = false;
    return true;
}

/*───────────────────────────────────────────────────────────────*/
/*  small query helpers                                          */
/*───────────────────────────────────────────────────────────────*/
size_t D8WBank::textureCount(size_t p) const
{
    return p < texBuf_.size() ? texBuf_[p].tex.size() : 0;
}
const TextureHdr& D8WBank::texture(size_t p,size_t i) const
{
    return texBuf_[p].tex[i];
}
bool D8WBank::isTextureModified(size_t p,size_t i) const
{
    return p < texBuf_.size() && i < texBuf_[p].tex.size()
         ? texBuf_[p].tex[i].modified : false;
}

/*───────────────────────────────────────────────────────────────*/
/*  export  ( *.ddt )                                            */
/*───────────────────────────────────────────────────────────────*/
bool D8WBank::exportTexture(size_t p,size_t i,const std::string& path) const
{
    if (!tBuf_ || p >= texBuf_.size() || i >= texBuf_[p].tex.size())
        return false;

    const TextureHdrEx& h = texBuf_[p].tex[i];
    if (h.fileOff + h.size > tBuf_->size()) return false;

    HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD bw;
    /* header without first 4 bytes */
    WriteFile(f, ((BYTE*)&h) + 4, sizeof(TextureHdr) - 4, &bw, NULL);
    WriteFile(f, &(*tBuf_)[h.fileOff], h.size, &bw, NULL);
    CloseHandle(f);
    return true;
}
bool D8WBank::exportTextureSet(size_t p,const std::string& dir) const
{
    if (p >= texBuf_.size()) return false;
    if (!ensureDir(dir))     return false;

    char fn[260];
    size_t i;
    for (i = 0; i < texBuf_[p].tex.size(); ++i)
    {
        sprintf_s(fn, "%s\\Tex%d%04d.ddt", dir.c_str(), (int)p, (int)i);
        if (!exportTexture(p, i, fn)) return false;
    }
    return true;
}

/*───────────────────────────────────────────────────────────────*/
/*  convert ( *.dds )                                            */
/*───────────────────────────────────────────────────────────────*/
static bool writeDDSHeader(HANDLE f,const TextureHdr& h)
{
    BYTE buf[128] = {0};
    BYTE* p = buf;

    *(DWORD*)p = DDS_MAGIC; p += 4;
    wr<uint32_t>(p, 124);

    uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH |
                     DDSD_PIXELFORMAT | DDSD_LINEARSIZE;
    if (h.mipCnt > 1) flags |= DDSD_MIPMAPCOUNT;
    wr<uint32_t>(p, flags);

    wr<uint32_t>(p, h.height);
    wr<uint32_t>(p, h.width);
    wr<uint32_t>(p, h.size);        /* linear size */
    wr<uint32_t>(p, 0);             /* depth       */
    wr<uint32_t>(p, h.mipCnt);

    p += 11 * 4;                    /* reserved    */

    wr<uint32_t>(p, 32);            /* pfSize      */

    uint32_t pfFlags = (h.type == 0x31545844 || h.type == 0x35545844)
                       ? DDPF_FOURCC : (DDPF_RGB | DDPF_ALPHAPIXELS);
    wr<uint32_t>(p, pfFlags);
    wr<uint32_t>(p, h.type);

    if (pfFlags & DDPF_FOURCC)
    {
        p += 20;                    /* skip rest of PIXELFORMAT       */
    }
    else
    {
        wr<uint32_t>(p, 32);
        wr<uint32_t>(p, 0x00FF0000);
        wr<uint32_t>(p, 0x0000FF00);
        wr<uint32_t>(p, 0x000000FF);
        wr<uint32_t>(p, 0xFF000000);
    }

    uint32_t caps = DDSCAPS_TEXTURE;
    if (h.mipCnt > 1) caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
    wr<uint32_t>(p, caps);
    p += 16;                        /* caps2,3,4 + reserved2 */

    DWORD bw;
    return WriteFile(f, buf, 128, &bw, NULL) && bw == 128;
}

bool D8WBank::convertTexture(size_t p,size_t i,const std::string& out) const
{
    if (!tBuf_ || p >= texBuf_.size() || i >= texBuf_[p].tex.size())
        return false;

    const TextureHdrEx& h = texBuf_[p].tex[i];
    if (h.fileOff + h.size > tBuf_->size()) return false;

    HANDLE f = CreateFileA(out.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;

    bool ok = writeDDSHeader(f, h);
    if (ok)
    {
        DWORD bw;
        ok = WriteFile(f, &(*tBuf_)[h.fileOff], h.size, &bw, NULL)
          && bw == h.size;
    }
    CloseHandle(f);
    return ok;
}
bool D8WBank::convertTextureSet(size_t p,const std::string& dir) const
{
    if (p >= texBuf_.size()) return false;
    ensureDir(dir);

    char fn[260];
    size_t i;
    for (i = 0; i < texBuf_[p].tex.size(); ++i)
    {
        sprintf_s(fn, "%s\\Tex%d%04d.dds", dir.c_str(), (int)p, (int)i);
        if (!convertTexture(p, i, fn)) return false;
    }
    return true;
}

/*───────────────────────────────────────────────────────────────*/
/*  import ( *.ddt )                                             */
/*───────────────────────────────────────────────────────────────*/
bool D8WBank::importTexture(size_t p,size_t i,const std::string& in)
{
    if (!tBuf_ || p >= texBuf_.size() || i >= texBuf_[p].tex.size())
        return false;

    std::vector<BYTE> tmp;
    if (!fileToMem(in, tmp) || tmp.size() < sizeof(TextureHdr) - 4)
        return false;

    TextureHdrEx newH;
    std::memset(&newH, 0, sizeof(newH));
    std::memcpy(((BYTE*)&newH) + 4, &tmp[0], sizeof(TextureHdr) - 4);

    size_t body = tmp.size() - (sizeof(TextureHdr) - 4);
    TextureHdrEx& old = texBuf_[p].tex[i];

    /* safety: allow equal or smaller replacement only                 */
    if (body > old.size || old.fileOff + body > tBuf_->size()) return false;

    std::memcpy(&(*tBuf_)[old.fileOff],
                &tmp[sizeof(TextureHdr) - 4],
                body);

    /* keep original fileOff / size */
    uint32_t keepSize = old.size;
    uint32_t keepOff  = old.fileOff;

    old = newH;
    old.size     = keepSize;
    old.fileOff  = keepOff;
    old.modified = true;
    dirty_ = true;
    return true;
}

bool D8WBank::importTextureSet(size_t p,const std::string& dir)
{
    if (!tBuf_ || p >= texBuf_.size()) return false;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.ddt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<std::string> files;
    do { files.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());

    size_t lim = std::min(files.size(), texBuf_[p].tex.size());
    char full[260];

    size_t i;
    for (i = 0; i < lim; ++i)
    {
        sprintf_s(full, "%s\\%s", dir.c_str(), files[i].c_str());
        importTexture(p, i, full);          /* ignore per-file errors */
    }
    return true;
}
