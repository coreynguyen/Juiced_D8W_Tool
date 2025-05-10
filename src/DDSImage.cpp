#include "DDSImage.h"
#include <wx/wfstream.h>
#include <wx/image.h>
#include <algorithm>
#include <vector>
#include <cstring>

#define FOURCC(a,b,c,d) ( uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24) )
static const uint32_t FOURCC_DDS  = FOURCC('D','D','S',' ');
static const uint32_t FOURCC_DXT1 = FOURCC('D','X','T','1');
static const uint32_t FOURCC_DXT3 = FOURCC('D','X','T','3');
static const uint32_t FOURCC_DXT5 = FOURCC('D','X','T','5');
static const uint32_t FOURCC_ATI2 = FOURCC('A','T','I','2');

/* small LUTs for 565 → 888 */
namespace {
struct Tables {
    unsigned char r5[32];
    unsigned char g6[64];
    Tables(){
        for(int i=0;i<32;++i) r5[i]=static_cast<unsigned char>((i<<3)|(i>>2));
        for(int i=0;i<64;++i) g6[i]=static_cast<unsigned char>((i<<2)|(i>>4));
    }
} LUT;
static inline unsigned char lerpB(unsigned char a,unsigned char b,bool w2of3){
    return w2of3 ? static_cast<unsigned char>((2*a+b)/3) : static_cast<unsigned char>((a+b)>>1);
}
} // anon

DDSImage::DDSImage():m_pixels(NULL),m_w(0),m_h(0),m_pitch(0),m_mipCount(1),m_fourCC(0){}
DDSImage::~DDSImage(){ freePixels(); }

void DDSImage::freePixels(){ delete[] m_pixels; m_pixels=NULL; }

/*──────────── public: LoadFromFile ───────────*/
bool DDSImage::LoadFromFile(const wxString& path)
{
    freePixels();
    wxFileInputStream in(path);
    if(!in.IsOk()) return false;

    DDSHeader hdr;
    if(!readHeader(in,hdr)) return false;

    m_w  = hdr.width;
    m_h  = hdr.height;
    m_pitch = m_w*4;
    m_mipCount = hdr.mipMapCount?hdr.mipMapCount:1;
    m_fourCC   = hdr.pf.fourCC;

    size_t bytes = size_t(m_pitch)*m_h;
    m_pixels = new unsigned char[bytes];
    std::memset(m_pixels,0,bytes);

    return decode(in,hdr);
}

/*──────────── header parse ───────────*/
bool DDSImage::readHeader(wxInputStream& in,DDSHeader& hdr)
{
    if(in.Read(&hdr,sizeof(hdr)).LastRead()!=sizeof(hdr)) return false;
    if(hdr.magic!=FOURCC_DDS || hdr.size!=124 || hdr.pf.size!=32) return false;
    return hdr.width && hdr.height;
}

/*──────────── master decode ───────────*/
bool DDSImage::decode(wxInputStream& in,const DDSHeader& hdr)
{
    const uint32_t fmt = hdr.pf.fourCC;

    if(fmt==FOURCC_DXT1||fmt==FOURCC_DXT3||fmt==FOURCC_DXT5||fmt==FOURCC_ATI2)
    {
        const unsigned blk = (fmt==FOURCC_DXT1)?8:16;
        const int bw=(m_w+3)>>2, bh=(m_h+3)>>2;
        size_t need=size_t(bw)*bh*blk;
        std::vector<unsigned char> comp(need);
        if(in.Read(&comp[0],need).LastRead()!=need) return false;

        const unsigned char* src=&comp[0];
        for(int by=0;by<bh;++by)
            for(int bx=0;bx<bw;++bx,src+=blk){
                switch(fmt){
                case FOURCC_DXT1: DecodeDXT1(src,bx,by); break;
                case FOURCC_DXT3: DecodeDXT3(src,bx,by); break;
                case FOURCC_DXT5: DecodeDXT5(src,bx,by); break;
                case FOURCC_ATI2: DecodeATI2(src,bx,by); break;
                }
            }
        return true;
    }

    if(hdr.pf.rgbBitCount==32){
        size_t n=size_t(m_pitch)*m_h;
        return in.Read(m_pixels,n).LastRead()==n;
    }
    return false;
}

/*──────────── utility ───────────*/
void DDSImage::expand565(uint16_t c, unsigned char& r,unsigned char& g,unsigned char& b){
    r=LUT.r5[(c>>11)&31];
    g=LUT.g6[(c>>5)&63];
    b=LUT.r5[c&31];
}

/*──────────── DXT decoders (kept your original) ───────────*/
void DDSImage::decodePlain32(const unsigned char*s,int y,int bpp){
    std::memcpy(m_pixels+y*m_pitch,s,m_pitch);
}
void DDSImage::DecodeDXT1(const unsigned char*s,int bx,int by)
{
    uint16_t c0=s[0]|(s[1]<<8), c1=s[2]|(s[3]<<8);
    unsigned char r0,g0,b0,r1,g1,b1;
    expand565(c0,r0,g0,b0); expand565(c1,r1,g1,b1);
    bool opaque=c0>c1;
    unsigned char clr[4][4]={
      {b0,g0,r0,255},{b1,g1,r1,255},
      {lerpB(b0,b1,1),lerpB(g0,g1,1),lerpB(r0,r1,1),255},
      {lerpB(b0,b1,0),lerpB(g0,g1,0),lerpB(r0,r1,0),opaque?255:0}};
    uint32_t idx= s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
    int x0=bx<<2, y0=by<<2;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(y0+py)*m_pitch+(x0<<2);
        for(int px=0;px<4;px+=2,idx>>=4){
            const unsigned char* cA=clr[idx&3];
            memcpy(dst,cA,4);
            const unsigned char* cB=clr[(idx>>2)&3];
            memcpy(dst+4,cB,4);
            dst+=8;
        }
    }
}
void DDSImage::DecodeDXT3(const unsigned char*s,int bx,int by)
{
    unsigned char alpha[16];
    for(int i=0;i<8;++i){
        unsigned v=s[i];
        alpha[i*2]=(v&15)*17;
        alpha[i*2+1]=((v>>4)&15)*17;
    }
    DecodeDXT1(s+8,bx,by);
    int x0=bx<<2,y0=by<<2;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(y0+py)*m_pitch+(x0<<2);
        for(int px=0;px<4;++px)
            dst[px*4+3]=alpha[py*4+px];
    }
}
void DDSImage::DecodeDXT5(const unsigned char*s,int bx,int by)
{
    unsigned a0=s[0],a1=s[1];
    unsigned char lut[8]={a0,a1};
    if(a0>a1) for(int k=1;k<=6;++k) lut[1+k]=(unsigned char)(((7-k)*a0+k*a1)/7);
    else{ for(int k=1;k<=4;++k) lut[1+k]=(unsigned char)(((5-k)*a0+k*a1)/5); lut[6]=0;lut[7]=255;}
    unsigned long long bits=0;
    for(int i=0;i<6;++i) bits|=(unsigned long long)s[2+i]<<(8*i);
    unsigned char alpha[16];
    for(int i=0;i<16;++i) alpha[i]=lut[(bits>>(3*i))&7];
    DecodeDXT1(s+8,bx,by);
    int x0=bx<<2,y0=by<<2;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(y0+py)*m_pitch+(x0<<2);
        for(int px=0;px<4;++px) dst[px*4+3]=alpha[py*4+px];
    }
}
void DDSImage::DecodeATI2(const unsigned char*s,int bx,int by)
{
    auto exp=[&](const unsigned char*q,unsigned char out[16]){
        unsigned a0=q[0],a1=q[1]; unsigned char lut[8]={a0,a1};
        if(a0>a1) for(int k=1;k<=6;++k) lut[1+k]=(unsigned char)(((7-k)*a0+k*a1)/7);
        else{ for(int k=1;k<=4;++k) lut[1+k]=(unsigned char)(((5-k)*a0+k*a1)/5); lut[6]=0; lut[7]=255;}
        unsigned long long bits=0; for(int i=0;i<6;++i) bits|=(unsigned long long)q[2+i]<<(8*i);
        for(int i=0;i<16;++i) out[i]=lut[(bits>>(3*i))&7];
    };
    unsigned char R[16],G[16]; exp(s,R); exp(s+8,G);
    int x0=bx<<2,y0=by<<2, idx;
    for(int py=0;py<4;++py){
        unsigned char* dst=m_pixels+(y0+py)*m_pitch+(x0<<2);
        for(int px=0;px<4;++px,++dst){
            idx=py*4+px;
            dst[0]=R[idx]; dst[1]=G[idx]; dst[2]=127; dst[3]=255;
            dst+=3;
        }
    }
}

/*──────────── bitmap conversion ───────────*/
wxBitmap DDSImage::AsBitmap(int maxEdge, bool keepAlpha) const
{
    if(!m_pixels) return wxBitmap();

    wxImage img(m_w, m_h);
    img.InitAlpha();                        // ← allocate alpha buffer

    unsigned char* dstRGB = img.GetData();
    unsigned char* dstA   = img.GetAlpha(); // now non-NULL
    const unsigned char*  src = m_pixels;

    for(int y = 0; y < m_h; ++y, src += m_pitch)
    {
        for(int x = 0; x < m_w; ++x)
        {
            const unsigned char* px = src + x*4;   // BGRA
            int ofs = (y*m_w + x);
            dstRGB[ofs*3 + 0] = px[2];             // R
            dstRGB[ofs*3 + 1] = px[1];             // G
            dstRGB[ofs*3 + 2] = px[0];             // B
            dstA  [ofs]       = keepAlpha ? px[3] : 255;
        }
    }

    if(maxEdge > 0 && (m_w > maxEdge || m_h > maxEdge))
        img = img.Scale(maxEdge, maxEdge, wxIMAGE_QUALITY_HIGH);

    return wxBitmap(img);
}


/*──────────── info helpers (unchanged) ───────────*/
wxString DDSImage::GetFormat() const{
    switch(m_fourCC){
        case FOURCC_DXT1: return "DXT1";
        case FOURCC_DXT3: return "DXT3";
        case FOURCC_DXT5: return "DXT5";
        case FOURCC_ATI2: return "ATI2";
        default:          return "Unknown";
    }
}
wxString DDSImage::GetSize() const{
    return wxString::Format("%dx%d",m_w,m_h);
}
wxString DDSImage::GetMipCount() const{
    return wxString::Format("Mips: %d",m_mipCount);
}
wxString DDSImage::GetMemoryUsage() const{
    size_t raw=m_w*m_h*4;
    return wxString::Format("Mem: %.1f KB", raw/1024.0);
}
