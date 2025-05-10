#ifndef DDSIMAGE_H
#define DDSIMAGE_H

#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/stream.h>

#pragma pack(push,1)
struct DDSPixelFormat
{
    uint32_t size, flags, fourCC, rgbBitCount;
    uint32_t rMask, gMask, bMask, aMask;
};
struct DDSHeader
{
    uint32_t magic, size, flags;
    uint32_t height, width;
    uint32_t pitchOrLinearSize, depth, mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pf;
    uint32_t caps, caps2, caps3, caps4, reserved2;
};
#pragma pack(pop)

/*──────────────────────────────────────────────────────────────
    Simple DDS decoder → BGRA8 in RAM → wxBitmap
──────────────────────────────────────────────────────────────*/
class DDSImage
{
public:
    DDSImage();
    ~DDSImage();

    bool        LoadFromFile(const wxString& path);   // returns true on success
    wxBitmap AsBitmap(int maxEdge = 0, bool keepAlpha = true) const;


    /* infos for status bar / tooltip */
    wxString    GetFormat()      const;
    wxString    GetSize()        const;
    wxString    GetMipCount()    const;
    wxString    GetMemoryUsage() const;

private:
    /* helpers */
    bool        readHeader(wxInputStream&, DDSHeader&);
    bool        decode(wxInputStream&, const DDSHeader&);
    void        DecodeDXT1 (const unsigned char*, int bx,int by);
    void        DecodeDXT3 (const unsigned char*, int bx,int by);
    void        DecodeDXT5 (const unsigned char*, int bx,int by);
    void        DecodeATI2(const unsigned char*, int bx,int by);
    void        decodePlain32(const unsigned char*, int y, int bpp);
    static void expand565(uint16_t, unsigned char&, unsigned char&, unsigned char&);

    void        freePixels();

    unsigned char* m_pixels;
    int            m_w, m_h, m_pitch;
    int            m_mipCount;
    uint32_t       m_fourCC;
};
#endif
