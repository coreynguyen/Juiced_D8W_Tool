


#include "d8w_parser.h"

#include <windows.h>
#include <direct.h>
#include <algorithm>
#include <cstring>
#include <map>

#define DDS_MAGIC 0x20534444
#define DDSD_CAPS 0x00000001
#define DDSD_HEIGHT 0x00000002
#define DDSD_WIDTH 0x00000004
#define DDSD_PIXELFORMAT 0x00001000
#define DDSD_MIPMAPCOUNT 0x00020000
#define DDSD_LINEARSIZE 0x00080000
#define DDPF_ALPHAPIXELS 0x00000001
#define DDPF_FOURCC 0x00000004
#define DDPF_RGB 0x00000040
#define DDSCAPS_TEXTURE 0x00001000
#define DDSCAPS_COMPLEX 0x00000008
#define DDSCAPS_MIPMAP 0x00400000




/* ========================================================================== */
/*  Debug / error infrastructure                                              */
/*  ------------------------------------------------------------------------ */
/*  • SETERR()  – write formatted message into gLastErr                       */
/*  • DBGPOP()  – message-box only in a _DEBUG build                          */
/*  • DBGBOX()  – printf-style shorthand used throughout the parser           */
/* ========================================================================== */
std::string gLastErr;

static void setErr(const char* fmt, ...)     // printf → gLastErr
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gLastErr = buf;
}

static void pop(const char* fmt, ...)        // printf → MessageBoxA
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    MessageBoxA(nullptr, buf, "d8w-debug", MB_OK | MB_ICONINFORMATION);
}

/* ========================================================================== */
/*  Debug / error infrastructure (works pre-C++11)                            */
/* ========================================================================== */
#include <cstdarg>     /* va_list, va_start, …                                */



/* -------- portable snprintf / vsnprintf ---------------------------------- */
#ifdef _MSC_VER
#   define SNPRINTF    _snprintf
#   define VSNPRINTF   _vsnprintf
#else
#   define SNPRINTF    snprintf
#   define VSNPRINTF   vsnprintf
#endif

/* ------------------------------------------------------------------------- */
/*  helper – write gLastErr                                                  */
/* ------------------------------------------------------------------------- */
static void SetErrF(const char* fmt, ...)        /* ALWAYS call through macro */
{
    char buf[256];

    va_list ap;
    va_start(ap, fmt);
    VSNPRINTF(buf, sizeof(buf), fmt, ap);
    buf[sizeof(buf)-1] = '\0';
    va_end(ap);

    gLastErr = buf;
}

/*  call like  SETERR("plain msg")  or  SETERR("fmt %d", n)                  */
#define SETERR(...)  SetErrF(__VA_ARGS__)

/* ------------------------------------------------------------------------- */
#ifdef _DEBUG                    /* message boxes only in debug builds       */
#   define DBGPOP(msg)  MessageBoxA(nullptr, (msg), "d8w-debug",             \
                                    MB_OK | MB_ICONERROR)

#   define DBGBOX(fmt, ...)                                                  \
        do { char _b_[256];                                                  \
             SNPRINTF(_b_, sizeof(_b_), (fmt), __VA_ARGS__);                 \
             MessageBoxA(nullptr, _b_, "d8w-debug",                          \
                         MB_OK | MB_ICONINFORMATION);                        \
        } while (0)
#else
#   define DBGPOP(msg)      ((void)0)
#   define DBGBOX(fmt, ...) ((void)0)
#endif
/* ========================================================================== */

/* ========================================================================== */


using namespace juiced;

namespace
{



template<typename T> inline T rd(const BYTE*& p)
{ T v; std::memcpy(&v, p, sizeof(T)); p += sizeof(T); return v; }

template<typename T> inline void wr(BYTE*& p, T v)
{ std::memcpy(p, &v, sizeof(T)); p += sizeof(T); }

static void pushLE32(std::vector<BYTE>& v, uint32_t x)
{
v.push_back( BYTE( x &0xFF) );
v.push_back( BYTE((x >> 8 ) &0xFF) );
v.push_back( BYTE((x >> 16) &0xFF) );
v.push_back( BYTE((x >> 24) &0xFF) );
}

/* -----------------------------------------------------------
   Read an entire file into a std::vector<BYTE>

   - returns true  on success   (dst is filled)
   - returns false on any error (dst is cleared)
   -----------------------------------------------------------*/
/* --------------------------------------------------------------
   Read an entire file into a vector<BYTE>
   Returns true on success, false on *any* error.
   --------------------------------------------------------------*/
static bool fileToMem(const std::string& path, std::vector<BYTE>& dst)
{
    dst.clear();

    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart == 0 || li.QuadPart > 0x7FFFFFFF)
    { CloseHandle(h); return false; }

    const DWORD bytesWanted = static_cast<DWORD>(li.QuadPart);
    dst.resize(bytesWanted);

    BYTE* ptr   = dst.data();
    DWORD bytes = bytesWanted;
    while (bytes > 0)
    {
        DWORD chunk = 0;
        if (!ReadFile(h, ptr, bytes, &chunk, nullptr) || chunk == 0)
        { CloseHandle(h); dst.clear(); return false; }

        ptr   += chunk;
        bytes -= chunk;
    }
    CloseHandle(h);
    return true;
}

static bool ensureDir(const std::string& d)
{
DWORD a = GetFileAttributesA(d.c_str());
return (a!=INVALID_FILE_ATTRIBUTES) || (_mkdir(d.c_str())==0);
}

static bool writeDDSHeader(HANDLE f,const TextureHdr& h)
{
BYTE buf[128]={0}; BYTE* p=buf;

*(DWORD*)p = DDS_MAGIC; p+=4;
wr<uint32_t>(p,124);

uint32_t flags = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|
DDSD_PIXELFORMAT|DDSD_LINEARSIZE;
if(h.mipCnt>1) flags|=DDSD_MIPMAPCOUNT;
wr<uint32_t>(p,flags);

wr<uint32_t>(p,h.height);
wr<uint32_t>(p,h.width );
wr<uint32_t>(p,h.size );
wr<uint32_t>(p,0);
wr<uint32_t>(p,h.mipCnt);

p += 11*4;

wr<uint32_t>(p,32);

uint32_t pf = (h.type==0x31545844 || h.type==0x35545844)
? DDPF_FOURCC : (DDPF_RGB|DDPF_ALPHAPIXELS);
wr<uint32_t>(p,pf);
wr<uint32_t>(p,h.type);

if(pf & DDPF_FOURCC) p += 20;
else
{
wr<uint32_t>(p,32); wr<uint32_t>(p,0x00FF0000);
wr<uint32_t>(p,0x0000FF00); wr<uint32_t>(p,0x000000FF);
wr<uint32_t>(p,0xFF000000);
}

uint32_t caps = DDSCAPS_TEXTURE;
if(h.mipCnt>1) caps |= DDSCAPS_COMPLEX|DDSCAPS_MIPMAP;
wr<uint32_t>(p,caps);
p += 16;

DWORD bw; return WriteFile(f,buf,128,&bw,NULL) && bw==128;
}
}

typedef std::map< uint32_t, std::vector<juiced::Reference*> > RefMap;

static RefMap gRefIdx;
static std::vector<juiced::D8WBank*> gBanks;

static void addRef(uint32_t abs, juiced::Reference* r)
{
gRefIdx[abs].push_back(r);
}

static void rebuildIndex()
{
gRefIdx.clear();

size_t bi, pi, ti;
for (bi = 0; bi < gBanks.size(); ++bi)
{

std::vector<juiced::TextureTable>& tbls = gBanks[bi]->tables();

for (pi = 0; pi < tbls.size(); ++pi)
{
for (ti = 0; ti < tbls[pi].refs.size(); ++ti)
{
juiced::Reference* ref = &tbls[pi].refs[ti];
addRef(ref->hdr->fileOff, ref);
}
}
}
}

static inline std::vector<BYTE>* bigBuf()
{
return gBanks.empty() ? NULL : gBanks.front()->tBuffer();
}

/******************************************************************************
* spliceReplace  –  in-place replace / resize a slice inside the shared *.d8t
*
* big      : reference to the in-memory .d8t byte vector
* abs      : absolute byte offset (start of the *body* to replace)
* oldSz    : byte length of the existing body
* newData  : pointer to the replacement body (may be NULL when newSz == 0)
* newSz    : byte length of that replacement body
* delta    : (out) newSz – oldSz  →  caller adds this to every header/offset
*
* RETURNS  : true  – splice succeeded
*            false – bounds or parameter error (gLastErr is set)
*
* Behaviour
* ─────────
* • Preserves every unknown byte before and after the slice.
* • Works even if the underlying std::vector reallocates.
* • Zero-copy path when the new payload is the same size.
* • Never touches global tables; caller (`importTexture`) does that.
******************************************************************************/
static bool spliceReplace(std::vector<BYTE>& big,
                          uint32_t           abs,
                          uint32_t           oldSz,
                          const BYTE*        newData,
                          uint32_t           newSz,
                          int32_t&           delta /* out */)
{
    /* ── 0. sanity guards ──────────────────────────────────────────── */
    delta = 0;

    const size_t fileSz = big.size();
    if (abs > fileSz || abs + oldSz > fileSz)                  /* OOB   */
    {
        SETERR("spliceReplace: out-of-bounds (pos=0x%08X old=%u file=%zu)",
               abs, oldSz, fileSz);
        return false;
    }
    if (newSz && newData == NULL)                              /* bad ptr */
    {
        SETERR("spliceReplace: newData is NULL while newSz = %u", newSz);
        return false;
    }

    /* ── 1. size reconciliation ───────────────────────────────────── */
    delta = (int32_t)newSz - (int32_t)oldSz;

    if (delta > 0)   /* grow: make room AFTER the old body */
        big.insert(big.begin() + abs + oldSz, delta, 0);

    else if (delta < 0)   /* shrink: snip the surplus bytes */
        big.erase (big.begin() + abs + newSz,
                   big.begin() + abs + oldSz);
    /* delta == 0 → length unchanged, buffer already correct */

    /* ── 2. copy the new payload ──────────────────────────────────── */
    if (newSz)   /* (legal to be zero – that would “delete” the body) */
        std::memcpy(&big[abs], newData, newSz);

#ifdef _DEBUG
    DBGBOX("spliceReplace ✔ abs=0x%08X  old=%u  new=%u  Δ=%d  newFile=%zu",
           abs, oldSz, newSz, delta, big.size());
#endif
    return true;
}



bool D8TFile::loadFileToMem(const std::string& p,std::vector<BYTE>& dst) const
{ return fileToMem(p,dst); }

bool D8TFile::load(const std::string& p)
{
pathT_=p; return loadFileToMem(p,buf_);
}

D8WBank::D8WBank(): dirty_(false), tBuf_(NULL), headerFixed(false) {}
D8WBank::~D8WBank()
{
    /* ─── 1. remove “this” from gBanks ────────────────────────── */
    size_t i;
    for (i = 0; i < gBanks.size(); )
    {
        if (gBanks[i] == this)
            gBanks.erase(gBanks.begin() + i);
        else
            ++i;
    }

    /* ─── 2. scrub every Reference that still points here ─────── */
    RefMap::iterator it = gRefIdx.begin();
    while (it != gRefIdx.end())
    {
        std::vector<juiced::Reference*>& v = it->second;

        /* erase-by-index so we stay portable pre-C++11 */
        size_t k;
        for (k = 0; k < v.size(); )
        {
            juiced::Reference* r = v[k];
            if (!r || r->ownerBank == this)
                v.erase(v.begin() + k);   /* remove zombie */
            else
                ++k;
        }

        /* drop empty map buckets altogether */
        if (v.empty())
            it = gRefIdx.erase(it);
        else
            ++it;
    }
}




bool D8WBank::locateD8T(const std::string& folder,
const std::string& stem,
const std::string& hint,
std::string& out) const
{
if(!hint.empty()){ out=hint; return true; }

out = folder + "\\" + stem + ".d8t";
if(GetFileAttributesA(out.c_str())!=INVALID_FILE_ATTRIBUTES) return true;

WIN32_FIND_DATAA fd;
HANDLE h = FindFirstFileA((folder+"\\*.d8t").c_str(),&fd);
if(h!=INVALID_HANDLE_VALUE){ out=folder+"\\"+fd.cFileName; FindClose(h); return true; }
return false;
}

bool D8WBank::load(const std::string& wPath,
                   const std::vector<BYTE>& sharedTbuf)
{
    /* keep the shared big-bank buffer -------------------------- */
    tBuf_  = const_cast< std::vector<BYTE>* >(&sharedTbuf);
    pathW_ = wPath;

    /* derive folder / stem (for .d8t auto-locate) -------------- */
    size_t slash = wPath.find_last_of("\\/"); if(slash==std::string::npos) slash = 0;
    size_t dot   = wPath.find_last_of('.');  if(dot  ==std::string::npos)  dot   = wPath.size();

    std::string folder = wPath.substr(0 , slash);
    std::string stem   = wPath.substr(slash ? slash+1 : 0, dot-slash-1);

    locateD8T(folder, stem, "", pathT_);         /* fills pathT_ (best effort) */

    /* load whole *.d8w into RAM -------------------------------- */
    if(!fileToMem(wPath, wBuf_)) return false;

    const BYTE* p   = &wBuf_[0];
    const BYTE* end = p + wBuf_.size();
    if(end-p < 12) return false;

    const uint32_t totalTex = rd<uint32_t>(p);   /* not used – sanity only   */
    const uint32_t tblCnt   = rd<uint32_t>(p);
    const uint32_t totalSz  = rd<uint32_t>(p);   /* dito                      */

    /* ────────────────── texture tables ─────────────────────── */
    texBuf_.resize(tblCnt);

    uint32_t cursor = 0;               /* running absolute offset in *.d8t */
    size_t    pi    = 0, ti = 0;

    for(pi = 0; pi < texBuf_.size(); ++pi)
    {
        if(end-p < 12) return false;

        TextureTable& tbl = texBuf_[pi];

        tbl.skip = rd<uint32_t>(p);
        tbl.size = rd<uint32_t>(p);
        const uint32_t n = rd<uint32_t>(p);

        if(end-p < n * sizeof(TextureHdr)) return false;

        tbl.tex .resize(n);
        tbl.refs.resize(n);

        cursor += tbl.skip;            /* table starts after previous gap  */
        tbl.absOff = cursor;

        uint32_t off = cursor;         /* first texture’s absolute offset  */

        for(ti = 0; ti < n; ++ti)
        {
            /* copy raw 44-byte header */
            std::memcpy(&tbl.tex[ti], p, sizeof(TextureHdr));
            p += sizeof(TextureHdr);

            /* extend to ‘Ex’ --------------------------- */
            tbl.tex[ti].fileOff  = off;
            tbl.tex[ti].modified = false;

            /* build quick-reference for later splices -- */
            Reference& R = tbl.refs[ti];
            R.hdr        = &tbl.tex[ti];
            R.ownerBank  = this;
            R.pSetSize   = &tbl.size;         /* <── 2nd int  in table header */
            R.pFileTotal = (uint32_t*)&wBuf_[8]; /* <── 3rd int in global hdr */

            addRef(off, &R);                  /* push into global index      */

            off += tbl.tex[ti].size;          /* next texture starts here    */
        }
        cursor = tbl.absOff + tbl.size;       /* next table’s base offset    */
    }

    /* ────────────────── texture-set section ────────────────── */
    if(end-p < 4) return false;
    const uint32_t setCnt = rd<uint32_t>(p);
    texSet_.resize(setCnt);

    if(setCnt)
    {
        const BYTE* const setsBeg = p;        /* remember for stride detect  */

        /* detect stride (= columns per set) ------------------- */
        if(end-p < 32) return false;          /* need at least one name      */
        const uint32_t firstName = rd<uint32_t>(p);
        p += 28;                              /* skip rest of 32-byte name   */

        uint32_t stride = 0;
        const BYTE* probe = p;
        while(probe + 4 <= end)
        {
            if(rd<uint32_t>(probe) == firstName)
            {
                stride = (uint32_t)((probe - setsBeg - 36) / 4);
                break;
            }
        }
        if(stride == 0 && setCnt > 1) return false;

        /* rewind & really parse -------------------------------- */
        p = setsBeg;
        size_t si, k;
        for(si = 0; si < setCnt; ++si)
        {
            char nm[33] = {0};
            std::memcpy(nm, p, 32); p += 32;
            texSet_[si].name = nm;

            texSet_[si].indexTable.resize(stride);

            for(k = 0; k < stride; ++k)
            {
                const int32_t idx = rd<int32_t>(p);
                texSet_[si].indexTable[k] = idx;

                /* hook reference → this table’s SIZE + global total ---- */
                if(idx >= 0)
                {
                    /* idx is a global linear index across ALL tables     */
                    size_t pack  = 0;
                    size_t local = (size_t)idx;

                    for(pack = 0; pack < texBuf_.size(); ++pack)
                    {
                        if(local < texBuf_[pack].tex.size()) break;
                        local -= texBuf_[pack].tex.size();
                    }
                    if(pack < texBuf_.size())
                    {
                        Reference& R = texBuf_[pack].refs[local];
                        R.pSetSize   = &texBuf_[pack].size;       /* ensure */
                        R.pFileTotal = (uint32_t*)&wBuf_[8];
                    }
                }
            }
        }
    }

    /* ─────────── tail (unknown block) – keep verbatim ───────── */
    tailRaw_.assign(p, end);

    /* ─────────── register in globals & rebuild index ────────── */
    gBanks.push_back(this);
    rebuildIndex();

    dirty_       = false;
    headerFixed  = false;
    return true;
}


/*******************************************************************************
*  D8WBank::save  –  write one playlist (*.d8w) and, once, the big bank (*.d8t)
*******************************************************************************/
/* ───────────────── helper: push little-endian DWORD ─────────────────── */
static inline void le32push(std::vector<BYTE>& v, uint32_t x)
{
    v.push_back( BYTE( x        & 0xFF));
    v.push_back( BYTE((x >>  8) & 0xFF));
    v.push_back( BYTE((x >> 16) & 0xFF));
    v.push_back( BYTE((x >> 24) & 0xFF));
}

/* ───────── helper: dump whole buffer to file (share-write!) ──────────── */
/* helper used by save() – write an entire vector to disk */
static bool dumpWhole(const std::string& path,
                      const std::vector<BYTE>& data)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD done = 0;
    const BOOL ok = WriteFile(h, data.data(),
                              static_cast<DWORD>(data.size()),
                              &done, nullptr);
    CloseHandle(h);
    return ok && done == data.size();
}

/* ───────────────────────────  D8WBank::save  ────────────────────────── */
bool D8WBank::save(const std::string& outW, const std::string& outT)
{
    /* nothing changed or no big-buffer pointer? */
    if (!dirty_ || !tBuf_) return false;

    /* ── 1. rebuild fresh *.d8w into wOut ─────────────────────────────── */
    std::vector<BYTE> wOut;
    wOut.reserve(wBuf_.size());                /* upper bound */

    /* global header ---------------------------------------------------- */
    uint32_t totalTex = 0, totalSz = 0;
    for (size_t i = 0; i < texBuf_.size(); ++i)
    {
        totalTex += (uint32_t)texBuf_[i].tex.size();
        totalSz  += texBuf_[i].size;
    }
    le32push(wOut, totalTex);
    le32push(wOut, (uint32_t)texBuf_.size());
    le32push(wOut, totalSz);

    /* tables ----------------------------------------------------------- */
    uint32_t cursor = 0;
    for (size_t i = 0; i < texBuf_.size(); ++i)
    {
        const TextureTable& tbl = texBuf_[i];

        le32push(wOut, tbl.absOff - cursor);          /* skip   */
        le32push(wOut, tbl.size);                     /* size   */
        le32push(wOut, (uint32_t)tbl.tex.size());     /* count  */

        for (size_t t = 0; t < tbl.tex.size(); ++t)
        {
            const BYTE* src = (const BYTE*)&tbl.tex[t];
            wOut.insert(wOut.end(), src, src + sizeof(TextureHdr));
        }
        cursor = tbl.absOff + tbl.size;
    }

    /* unknown tail – copy verbatim ------------------------------------ */
    wOut.insert(wOut.end(), tailRaw_.begin(), tailRaw_.end());

    /* ── 2. write to disk ────────────────────────────────────────────── */
    if (!dumpWhole(outW, wOut))                     /* *.d8w */
        return false;

    if (!outT.empty() && !dumpWhole(outT, *tBuf_)) /* *.d8t, once */
        return false;

    /* ── 3. keep our in-memory buffer in sync (avoid double deltas) ─── */
    wBuf_.assign(wOut.begin(), wOut.end());

    /* ── 4. clear dirty / headerFixed for every bank sharing this big T ─*/
    for (size_t b = 0; b < gBanks.size(); ++b)
    {
        if (gBanks[b]->tBuf_ != tBuf_) continue;

        gBanks[b]->dirty_      = false;
        gBanks[b]->headerFixed = false;

        for (size_t p = 0; p < gBanks[b]->texBuf_.size(); ++p)
            for (size_t k = 0; k < gBanks[b]->texBuf_[p].tex.size(); ++k)
                gBanks[b]->texBuf_[p].tex[k].modified = false;

        /* also copy fresh header into the partner banks’ wBuf_ */
        gBanks[b]->wBuf_.assign(wOut.begin(), wOut.end());
    }
    return true;
}




size_t D8WBank::textureCount(size_t p) const
{ return p<texBuf_.size()? texBuf_[p].tex.size():0; }

const TextureHdr& D8WBank::texture(size_t p,size_t i) const
{ return texBuf_[p].tex[i]; }

bool D8WBank::isTextureModified(size_t p,size_t i) const
{ return p<texBuf_.size()&&i<texBuf_[p].tex.size()? texBuf_[p].tex[i].modified:false; }

bool D8WBank::exportTexture(size_t p,size_t i,const std::string& path) const
{
if(!tBuf_||p>=texBuf_.size()||i>=texBuf_[p].tex.size()) return false;
const TextureHdrEx& h = texBuf_[p].tex[i];
if(h.fileOff+h.size > tBuf_->size()) return false;

HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,0,NULL,
CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
if(f==INVALID_HANDLE_VALUE) return false;
DWORD bw;
WriteFile(f,((BYTE*)&h)+4,sizeof(TextureHdr)-4,&bw,NULL);
WriteFile(f,&(*tBuf_)[h.fileOff],h.size,&bw,NULL);
CloseHandle(f); return true;
}
bool D8WBank::exportTextureSet(size_t p,const std::string& dir) const
{
if(p>=texBuf_.size()) return false;
if(!ensureDir(dir)) return false;
char fn[260];
size_t i; for(i=0;i<texBuf_[p].tex.size();++i){
sprintf_s(fn,"%s\\Tex%d%04d.ddt",dir.c_str(),(int)p,(int)i);
if(!exportTexture(p,i,fn)) return false;
}
return true;
}

bool D8WBank::convertTexture(size_t p,size_t i,const std::string& out) const
{
if(!tBuf_||p>=texBuf_.size()||i>=texBuf_[p].tex.size()) return false;
const TextureHdrEx& h = texBuf_[p].tex[i];
if(h.fileOff+h.size > tBuf_->size()) return false;

HANDLE f=CreateFileA(out.c_str(),GENERIC_WRITE,0,NULL,
CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
if(f==INVALID_HANDLE_VALUE) return false;
bool ok = writeDDSHeader(f,h);
if(ok){
DWORD bw;
ok = WriteFile(f,&(*tBuf_)[h.fileOff],h.size,&bw,NULL) && bw==h.size;
}
CloseHandle(f); return ok;
}
bool D8WBank::convertTextureSet(size_t p,const std::string& dir) const
{
if(p>=texBuf_.size()) return false; ensureDir(dir);
char fn[260]; size_t i;
for(i=0;i<texBuf_[p].tex.size();++i){
sprintf_s(fn,"%s\\Tex%d%04d.dds",dir.c_str(),(int)p,(int)i);
if(!convertTexture(p,i,fn)) return false;
}
return true;
}

static bool DDS2DDT(const BYTE* dds, size_t ddsSz, std::vector<BYTE>& out)
{
    DBGBOX("DDS2DDT  inSize=%u", (uint32_t)ddsSz);

    if (ddsSz < 128 || std::memcmp(dds, "DDS ", 4) != 0) return false;
    #pragma pack(push,1)
    struct DDSPF { uint32_t sz, flags, fourCC, rgbBits; uint32_t r,g,b,a; };
    struct DDSHdr {
        uint32_t magic, size, flags, h, w, pitchOrSize, depth, mips;
        uint32_t reserved1[11]; DDSPF pf;
        uint32_t caps,caps2,caps3,caps4, reserved2;
    };
    #pragma pack(pop)

    const DDSHdr* h = reinterpret_cast<const DDSHdr*>(dds);
    if (h->size != 124 || h->pf.sz != 32 || h->w == 0 || h->h == 0) return false;

    uint32_t fourCC = h->pf.fourCC;
    uint32_t juType = (fourCC==0x31545844||fourCC==0x35545844)
                    ? fourCC : 0x00000015;
    uint32_t body   = uint32_t(ddsSz - 128);

    TextureHdr hdr{};
    hdr.size   = body;
    hdr.type   = juType;
    hdr.width  = h->w;
    hdr.height = h->h;
    hdr.mipCnt = h->mips ? h->mips : 1;
    hdr.unk07  = hdr.unk08 = hdr.unk09 = 2;
    hdr.unk10  = hdr.unk11 = 1;
    hdr.unk12  = -1.5f;
    hdr.unk13  =  0.0f;

    out.resize(sizeof(hdr) + body);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), dds + 128, body);

    DBGBOX("DDS2DDT  ✔ ok  body=%u  type=0x%08X", body, juType);
    return true;
}

/* ------------------------------------------------------------------ */
/*  importTexture – replace ONE texture in the big bank (.d8t)        */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/*  importTexture – replace ONE texture in the big bank (.d8t)        */
/*  - pack / idx : which texture                                      */
/*  - inPath     : *.ddt  or  *.dds (auto-converted)                  */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/*  importTexture – replace ONE texture in the big bank (.d8t)        */
/* ------------------------------------------------------------------ */
/* ========================================================================== */
/*  2.  D8WBank::importTexture  –  *safe* one-texture replacement             */
/* ========================================================================== */
/* ------------------------------------------------------------------ */
/*  importTexture – replace ONE texture in the big bank (.d8t)        */
/*                                                                    */
/*  ‣ very defensive: verifies every pointer before use               */
/*  ‣ writes only to headers that are proven to live inside the       */
/*    owning bank’s tex-vector                                        */
/* ------------------------------------------------------------------ */


/*****************************************************************************
   D8WBank::importTexture  –  replace one texture inside the shared *.d8t
   =========================================================================
   • pack / idx : texture location inside *this* D8WBank
   • inPath     : .ddt  or  .dds  (DDS is auto-converted to DDT)
   • Returns    : true  on success,  false on any failure (gLastErr set)
*****************************************************************************/

static bool isLiveBank(juiced::D8WBank* p)          /* helper */
{
    size_t i;
    for (i = 0; i < gBanks.size(); ++i)
        if (gBanks[i] == p) return true;
    return false;
}

bool D8WBank::importTexture(size_t pack,
                            size_t idx,
                            const std::string& inPath)
{
    DBGBOX("importTexture  pack=%zu  idx=%zu  «%s»",
           pack, idx, inPath.c_str());

    /* ── 0. guards ─────────────────────────────────────────────── */
    if (!tBuf_)                              { SETERR("big-bank null");   return false; }
    if (pack >= texBuf_.size())              { SETERR("pack OOB");        return false; }
    if (idx  >= texBuf_[pack].tex.size())    { SETERR("index OOB");       return false; }

    /* ── 1. read file ──────────────────────────────────────────── */
    std::vector<BYTE> src;
    if (!fileToMem(inPath, src))
        return SETERR("file read fail"), false;

    /* ── 2. DDS → DDT (if needed) ──────────────────────────────── */
    std::vector<BYTE> ddt;
    if (src.size() >= 128 && std::memcmp(src.data(), "DDS ", 4) == 0)
    {
        if (!DDS2DDT(src.data(), src.size(), ddt))
            return SETERR("DDS2DDT fail"), false;
    }
    else
    {
        if (src.size() < sizeof(TextureHdr))
            return SETERR("DDT too small"), false;
        ddt.swap(src);                             /* already DDT */
    }

    /* ── 3. locate old body & sizes ────────────────────────────── */
    TextureHdrEx& old = texBuf_[pack].tex[idx];
    const uint32_t pos     = old.fileOff;
    const uint32_t oldBody = old.size;
    const uint32_t newBody = uint32_t(ddt.size() - sizeof(TextureHdr));

    /* ── 4. splice big-bank buffer ─────────────────────────────── */
    int32_t delta = 0;
    if (!spliceReplace(*tBuf_, pos, oldBody,
                       &ddt[sizeof(TextureHdr)], newBody, delta))
        return false;                                        /* gLastErr set */

    /* ── 5. craft fresh header ────────────────────────────────── */
    TextureHdrEx fresh;
    std::memset(&fresh, 0, sizeof(fresh));
    std::memcpy(reinterpret_cast<BYTE*>(&fresh)+4,
                &ddt[4], sizeof(TextureHdr) - 4);            /* skip “size” */
    fresh.size     = newBody;
    fresh.fileOff  = pos;
    fresh.modified = true;

    /* ── 6. shift tables & entries in *every* live bank ───────── */
    size_t b, t, k;
    for (b = 0; b < gBanks.size(); ++b)
    {
        D8WBank* bk = gBanks[b];
        if (!bk || bk->tBuf_ != tBuf_) continue;             /* diff .d8t */

        std::vector<TextureTable>& tbls = bk->tables();

        for (t = 0; t < tbls.size(); ++t)
        {
            TextureTable& tbl = tbls[t];

            if (tbl.absOff > pos)                /* tables after splice */
                tbl.absOff += delta;             /* (skip untouched)    */

            if (pos >= tbl.absOff && pos < tbl.absOff + tbl.size)
                tbl.size += delta;

            for (k = 0; k < tbl.tex.size(); ++k)
                if (tbl.tex[k].fileOff > pos)
                    tbl.tex[k].fileOff += delta;
        }
        bk->dirty_      = true;
        bk->headerFixed = true;
    }

    /* ── 7. rebuild ref-map so look-up is guaranteed ───────────── */
    rebuildIndex();

    /* ── 8. patch every Reference pointing at ‘pos’ ────────────── */
    RefMap::iterator node = gRefIdx.find(pos);
    if (node == gRefIdx.end())
    {
        /* Shouldn’t happen, but patch owner-bank to stay consistent */
        texBuf_[pack].tex[idx] = fresh;
        texBuf_[pack].size           += delta;
        *(uint32_t*)&wBuf_[8]        += delta;    /* global total in this .d8w */
        DBGPOP("importTexture: no ref node – patched caller only");
        return true;
    }

    size_t ok = 0, skip = 0;

    std::vector<juiced::Reference*>& refs = node->second;
    size_t r;
    for (r = 0; r < refs.size(); ++r)
    {
        juiced::Reference* ref = refs[r];
        if (!ref || !ref->hdr || !ref->ownerBank) { ++skip; continue; }
        if (!isLiveBank(ref->ownerBank))          { ++skip; continue; }

        /* —— make sure hdr really lives in one of ownerBank’s tables —— */
        const std::vector<TextureTable>& tbls2 = ref->ownerBank->tables();
        bool inRange = false;
        for (t = 0; t < tbls2.size() && !inRange; ++t)
        {
            const std::vector<TextureHdrEx>& texV = tbls2[t].tex;
            if (texV.empty()) continue;
            const TextureHdrEx* base = &texV.front();
            inRange = (ref->hdr >= base) &&
                      (ref->hdr <  base + texV.size());
        }
        if (!inRange) { ++skip; continue; }

        /* —— overwrite header & fix size fields ——————————————— */
        *(ref->hdr) = fresh;
        if (ref->pSetSize)   *(ref->pSetSize)   += delta;
        if (ref->pFileTotal) *(ref->pFileTotal) += delta;
        ++ok;
    }

    DBGBOX("importTexture ✔ old=%u  new=%u  Δ=%d  refs ok=%zu  skipped=%zu",
           oldBody, newBody, delta, ok, skip);
    return true;
}





bool D8WBank::importTextureSet(size_t pack, const std::string& dir)
{
    DBGBOX("importTextureSet  pack=%u  dir=\"%s\"",
           (uint32_t)pack, dir.c_str());

    if (!tBuf_ || pack >= texBuf_.size())
    {
        SETERR("Invalid pack index or uninitialized big-bank");
        DBGBOX("importTextureSet ✘ bad pack or big-bank");
        return false;
    }

    // 1) gather .ddt/.dds files
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.dd?").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const char* ext = strrchr(fd.cFileName, '.');
            if (!ext) continue;
            if (_stricmp(ext, ".ddt")==0 || _stricmp(ext, ".dds")==0)
                files.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    DBGBOX("importTextureSet  found %u candidate files",
           (uint32_t)files.size());
    if (files.empty())
    {
        SETERR("No .ddt/.dds files in \"%s\"", dir.c_str());
        return false;
    }

    // 2) sort so Tex0000N lines up with index N
    std::sort(files.begin(), files.end(),
              [](const std::string& a, const std::string& b){
                  return _stricmp(a.c_str(), b.c_str()) < 0;
              });

    size_t limit = std::min(files.size(), texBuf_[pack].tex.size());
    bool changed = false;
    char full[MAX_PATH];

    // 3) import one by one
    for (size_t i = 0; i < limit; ++i)
    {
        if (_snprintf(full, sizeof(full), "%s\\%s",
                      dir.c_str(), files[i].c_str()) < 0)
            continue;

        if (importTexture(pack, i, full))
            changed = true;
    }

    if (changed)
    {
        rebuildIndex();  // re-sync the global ref map
        DBGBOX("importTextureSet ✔ replaced %u of %u textures",
               (uint32_t)limit, (uint32_t)files.size());
    }
    else
    {
        SETERR("No textures replaced in pack %u", (uint32_t)pack);
        DBGBOX("importTextureSet ✘ no replacements");
    }

    return changed;
}



