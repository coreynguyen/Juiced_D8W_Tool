/*  Juiced – D8W Tool  (pre-C++11)  */
#include "d8wTool.h"

#include <fstream>
#include <algorithm>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/dir.h>
#include <wx/stdpaths.h>

/* ─── util: read whole file to memory ──────────────────────── */
static bool fileToMem(const wxString& path, std::vector<BYTE>& dst)
{
    std::ifstream f(path.mb_str(), std::ios::in|std::ios::binary);
    if(!f) return false;
    f.seekg(0,std::ios::end);
    size_t len = (size_t)f.tellg();
    f.seekg(0,std::ios::beg);
    dst.resize(len);
    f.read((char*)&dst[0], len);
    return !!f;
}

/* ─── util bitmaps ─────────────────────────────────────────── */
static wxBitmap MakeTransparent(int w=1,int h=1)
{
    wxImage img(w,h,true); img.InitAlpha(); *img.GetAlpha()=0;
    return wxBitmap(img);
}
static wxBitmap CompositeOnPink(const wxBitmap& src, bool showAlpha)
{
    if (!src.IsOk()) return src;

    wxImage img = src.ConvertToImage();

    if (!showAlpha)
    {
        // RGB only, remove transparency (make fully opaque)
        if (img.HasAlpha())
        {
            img.ClearAlpha();  // removes the alpha channel entirely
        }
    }

    wxBitmap dst(img.GetWidth(), img.GetHeight(), 24);
    wxMemoryDC dc(dst);
    dc.SetBackground(wxBrush(wxColour(255, 0, 255)));
    dc.Clear();
    dc.DrawBitmap(wxBitmap(img), 0, 0, false); // draw fully opaque
    dc.SelectObject(wxNullBitmap);

    return dst;
}


static wxString TempDDS()
{
    wxFileName t = wxFileName::CreateTempFileName(wxT("d8w"));
    t.SetExt(wxT("dds")); return t.GetFullPath();
}

/* -------------------------------------------------------------
   Build the display-string for a texture tree node
   “Tex<set><idx-5>  0x<off-8>  <fmt> [w x h]”
   -------------------------------------------------------------*/
static wxString makeTexLabel(const juiced::TextureHdrEx& h,
                             unsigned setIdx, unsigned texIdx)
{
    /* format (“DXT5” / “ARGB8888”) -------------------------------------- */
    wxString fmt;
    if (h.type == 0x15)          fmt = wxT("ARGB8888");
    else
    {
        char cc[5] = { char( h.type        & 0xFF),
                       char((h.type >>  8) & 0xFF),
                       char((h.type >> 16) & 0xFF),
                       char((h.type >> 24) & 0xFF), 0 };
        fmt = wxString::FromUTF8(cc);
    }

    return wxString::Format(wxT("Tex%u%05u  0x%08X  %s [%u x %u]"),
                            setIdx, texIdx,
                            h.fileOff,              // absolute offset
                            fmt.c_str(),
                            h.width, h.height);
}




/* ─── event table ──────────────────────────────────────────── */
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(wxID_OPEN , MainFrame::OnOpen )
    EVT_MENU(wxID_SAVE , MainFrame::OnSave )
    EVT_MENU(wxID_EXIT , MainFrame::OnExit )

    EVT_MENU(ID_Export , MainFrame::OnExport )
    EVT_MENU(ID_Convert, MainFrame::OnConvert)
    EVT_MENU(ID_Import , MainFrame::OnImport )

    EVT_MENU(ID_ZoomIn , MainFrame::OnZoomIn )
    EVT_MENU(ID_ZoomOut, MainFrame::OnZoomOut)
    EVT_MENU(ID_ToggleAlpha, MainFrame::OnToggleAlpha)

    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout )

    EVT_TREE_SEL_CHANGED     (ID_Tree, MainFrame::OnSelChanged )
    EVT_TREE_ITEM_RIGHT_CLICK(ID_Tree, MainFrame::OnTreeRClick)
wxEND_EVENT_TABLE()

/* ─── app bootstrap ───────────────────────────────────────── */
bool d8wToolApp::OnInit()
{
    MainFrame* f = new MainFrame(wxT("Juiced – D8W Tool"));
    f->Show();
    return true;
}

/* ─── ctor ─────────────────────────────────────────────────── */
MainFrame::MainFrame(const wxString& title)
        : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(800,580)),
          zoomPct_(100), showAlpha_(true)
{
    buildMenus();
    buildAccelerators();

    splitter_ = new wxSplitterWindow(this, wxID_ANY);

    tree_ = new wxTreeCtrl(splitter_, ID_Tree,
                           wxDefaultPosition, wxDefaultSize,
                           wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT);

    preview_ = new wxPanel(splitter_, wxID_ANY);
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
    infoText_ = new wxStaticText(preview_, wxID_ANY, wxT("Open a .d8t file…"));
    thumb_    = new wxStaticBitmap(preview_, wxID_ANY, MakeTransparent());
    vbox->Add(infoText_,0,wxALL|wxEXPAND,5);
    vbox->Add(thumb_,0,wxALL,5);
    preview_->SetSizer(vbox);

    splitter_->SplitVertically(tree_, preview_, 400);
    splitter_->SetMinimumPaneSize(200);
    wxBoxSizer* rootSz = new wxBoxSizer(wxVERTICAL);
    rootSz->Add(splitter_,1,wxEXPAND);
    SetSizer(rootSz);

    // Explicitly load and set your app icon here:
    SetIcon(wxICON(APP_ICON));

    Centre();
}


/* ────────────────────────────────────────────────────────────
                         File / tree helpers
   ────────────────────────────────────────────────────────────*/
void MainFrame::clearTree(){ tree_->DeleteAllItems(); }

static bool ieStartsWith(const wxString& a,const wxString& b)
{ return a.Left(b.Length()).CmpNoCase(b)==0; }

void MainFrame::populateTree()
{
    clearTree();
    if(bigTPath_.IsEmpty()){
        tree_->AddRoot(wxT("No file"));
        return;
    }
    wxTreeItemId root = tree_->AddRoot(wxFileName(bigTPath_).GetFullName());
    tree_->SetItemData(root,new TexItemData(-2,-1,-1));

    for(size_t b=0;b<banks_.size();++b)
    {
        wxTreeItemId wNode = tree_->AppendItem(root, wNames_[b]);
        tree_->SetItemData(wNode,new TexItemData((int)b,-1,-1));

        for(size_t p=0;p<banks_[b].texturePackCount();++p)
        {
            wxString lbl; lbl.Printf(wxT("TexSet%u"),(unsigned)p);
            wxTreeItemId packNode = tree_->AppendItem(wNode,lbl);
            tree_->SetItemData(packNode,new TexItemData((int)b,(int)p,-1));

            for(size_t t=0;t<banks_[b].textureCount(p);++t)
            {



                //wxString tl; tl.Printf(wxT("Tex%u%04u"),(unsigned)p,(unsigned)t);
                //wxTreeItemId texNode = tree_->AppendItem(packNode,tl);
                const juiced::TextureHdrEx& hEx = reinterpret_cast<const juiced::TextureHdrEx&>( banks_[b].texture(p,t));
                wxTreeItemId texNode = tree_->AppendItem( packNode, makeTexLabel(hEx, (unsigned)p, (unsigned)t));

                tree_->SetItemData(texNode,new TexItemData((int)b,(int)p,(int)t));
                if(banks_[b].isTextureModified(p,t))
                    tree_->SetItemTextColour(texNode,*wxRED);
            }
        }
    }
    tree_->Expand(root);
}

/* getSelection → bank / pack / tex (-1 where N/A) */
bool MainFrame::getSelection(int& bank,int& pack,int& tex) const
{
    wxTreeItemId id = tree_->GetSelection(); if(!id.IsOk()) return false;
    TexItemData* d = (TexItemData*)tree_->GetItemData(id);
    if(!d) return false;
    bank=d->bank; pack=d->pack; tex=d->tex; return true;
}

/* ─── open .d8t ────────────────────────────────────────────── */
void MainFrame::OnOpen(wxCommandEvent&)
{
    wxFileDialog dlg(this,wxT("Open .d8t"),
                     wxEmptyString,wxEmptyString,
                     wxT("d8t files (*.d8t)|*.d8t"),
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if(dlg.ShowModal()!=wxID_OK) return;

    /* load big texture bank ------------------------------------------------*/
    bigTPath_ = dlg.GetPath();
    if(!fileToMem(bigTPath_,bigT_)){
        wxMessageBox(wxT("Failed to load .d8t"),wxT("Error"),wxICON_ERROR);
        bigT_.clear(); return;
    }

    /* discover companion .d8w files ---------------------------------------*/
    wxFileName fn(bigTPath_);
    wxString folder = fn.GetPath();
    wxString stem   = fn.GetName();          // "Angel SuperSpeedway"
    wxArrayString found;
    wxDir::GetAllFiles(folder,&found,wxT("*.d8w"),wxDIR_FILES);
    banks_.clear(); wNames_.clear();

    for(size_t i=0;i<found.size();++i)
        if(ieStartsWith(wxFileName(found[i]).GetName(),stem))
        {
            juiced::D8WBank b;
            if(b.load(std::string(found[i].mb_str()), bigT_)){
                banks_.push_back(b);
                wNames_.push_back(wxFileName(found[i]).GetFullName());
            }
        }
    if(banks_.empty()){
        wxMessageBox(wxT("No matching .d8w files found"),wxT("Error"),wxICON_ERROR);
        bigT_.clear(); bigTPath_.Clear(); return;
    }

    /* reset preview & fill tree -------------------------------------------*/
    rawBmp_ = wxBitmap();   /* empty bitmap, no resource lookup */

    zoomPct_=100; showAlpha_=true;
    populateTree(); updateTitle();
}

/* ─── save (writes .d8t once, every dirty .d8w) ────────────────────────── */
void MainFrame::OnSave(wxCommandEvent&)
{
    bool anyDirty=false;
    for(size_t b=0;b<banks_.size();++b)
        if(banks_[b].isDirty()) anyDirty=true;
    if(!anyDirty){ wxBell(); return; }

    /* write .d8t once via the *first* dirty bank ---------------------------*/
    bool wroteBig=false;
    for(size_t b=0;b<banks_.size();++b)
        if(banks_[b].isDirty())
        {
            wxString wFull = wxFileName(bigTPath_).GetPathWithSep()
                           + wNames_[b];
            if(!banks_[b].save(std::string(wFull.mb_str()),
                               wroteBig ? "" : std::string(bigTPath_.mb_str())))
            {
                wxMessageBox(wxT("Save failed"),wxT("Error"),wxICON_ERROR);
                return;
            }
            wroteBig=true;
        }
    populateTree(); updateTitle();
}

/* ─── title bar ───────────────────────────────────────────── */
void MainFrame::updateTitle()
{
    wxString t = wxT("Juiced – D8W Tool");
    if(!bigTPath_.IsEmpty()) t += wxT("  [") + wxFileName(bigTPath_).GetFullName() + wxT(']');
    for(size_t b=0;b<banks_.size();++b) if(banks_[b].isDirty()){ t += wxT(" *"); break; }
    SetTitle(t);
}

/* ─── tree selection changed ──────────────────────────────── */
void MainFrame::OnSelChanged(wxTreeEvent&)
{
    int b,p,t; if(!getSelection(b,p,t)) return;
    if(b<0){ infoText_->SetLabel(wxT("")); return; }         // root
    if(p<0)      showWInfo (b);
    else if(t<0) showPackInfo(b,p);
    else         showTexInfo (b,p,t);
}

/* info helpers ────────────────────────────────────────────── */
void MainFrame::showWInfo(int b)
{
    zoomPct_=100; rawBmp_=MakeTransparent();
    infoText_->SetLabel(wxString::Format(wxT("%s\nPacks: %zu"),
                       wNames_[b].c_str(), banks_[b].texturePackCount()));
    thumb_->SetBitmap(MakeTransparent()); preview_->Layout();
}
void MainFrame::showPackInfo(int b,int p)
{
    zoomPct_=100; rawBmp_=MakeTransparent();
    infoText_->SetLabel(wxString::Format(wxT("%s / TexSet%u\nTextures: %zu"),
                       wNames_[b].c_str(),p,banks_[b].textureCount(p)));
    thumb_->SetBitmap(MakeTransparent()); preview_->Layout();
}

/* helper to pad to 18 chars */
static wxString col(const wxString& s0)
{
    wxString s(s0); while(s.length()<18) s+=wxT(' ');
    if(s.length()>18) s.Truncate(18); return s;
}



void MainFrame::showTexInfo(int bank,int pack,int tex)
{
    /* obtain full header (we stored TextureHdrEx internally) */
    const juiced::TextureHdrEx& h =
        reinterpret_cast<const juiced::TextureHdrEx&>(
            banks_[bank].texture(pack,tex));

    /* --- Four-character code → readable text -------------------- */
    wxString fcc;
    if (h.type == 0x15)                       fcc = wxT("ARGB8888");
    else
    {
        char cc[5] =
        {   char( h.type        & 0xFF),
            char((h.type >>  8) & 0xFF),
            char((h.type >> 16) & 0xFF),
            char((h.type >> 24) & 0xFF), 0 };
        fcc = wxString::FromUTF8(cc);
    }

    /* helper that pads / truncates to 18 chars */
    const wxString c0 = col(wxString::Format(wxT("Tex%04d"), tex));
    const wxString c1 = col(wxString::Format(wxT("%ux%u"),  h.width,  h.height));
    const wxString c2 = col(wxString::Format(wxT("Size:%u"),h.size));

    const wxString c3 = col(wxString::Format(wxT("Type:%s"),fcc.c_str()));
    const wxString c4 = col(wxString::Format(wxT("Mips:%u"),h.mipCnt));
    const wxString c5 = col(wxString::Format(wxT("u07:%u"), h.unk07));

    const wxString c6 = col(wxString::Format(wxT("u08:%u"),h.unk08));
    const wxString c7 = col(wxString::Format(wxT("u09:%u"),h.unk09));
    const wxString c8 = col(wxString::Format(wxT("u10:%u"),h.unk10));

    const wxString c9  = col(wxString::Format(wxT("u11:%u"),  h.unk11));
    const wxString c10 = col(wxString::Format(wxT("u12:%.2f"),h.unk12));
    const wxString c11 = col(wxString::Format(wxT("u13:%.2f"),h.unk13));

    /* NEW  ➜  absolute offset inside the big .d8t (hex) */
    const wxString c12 = col(wxString::Format(wxT("Off:0x%08X"),h.fileOff));

    /* assemble 4 rows × 3 columns (pad helper keeps width neat) */
    wxString info =
        c0 + c1 + c2 + wxT("\n") +
        c3 + c4 + c5 + wxT("\n") +
        c6 + c7 + c8 + wxT("\n") +
        c9 + c10+ c11 + wxT("\n") +   // existing four rows
        c12;                          // address shown on a 5-th short row

    infoText_->SetLabel(info);

    /* --- thumbnail ------------------------------------------------------- */
    rawBmp_ = wxBitmap();          // reset (no resource lookup!)
    zoomPct_ = 100;

    wxString tmp = TempDDS();
    if (banks_[bank].convertTexture(pack, tex, std::string(tmp.mb_str())))
    {
        DDSImage img;
        if (img.LoadFromFile(tmp))
            rawBmp_ = img.AsBitmap(0, /*keep alpha*/ true);
        wxRemoveFile(tmp);
    }
    if (!rawBmp_.IsOk())
        rawBmp_ = MakeTransparent();

    applyZoom();
}

/* ─── export / convert / import (bank aware) ───────────────── */
void MainFrame::OnExport(wxCommandEvent&)
{
    int b,p,t; if(!getSelection(b,p,t)) return;
    if(t>=0){
        wxFileDialog fd(this,wxT("Export .ddt"),wxEmptyString,wxEmptyString,
                        wxT("*.ddt"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(fd.ShowModal()==wxID_OK)
            banks_[b].exportTexture(p,t,std::string(fd.GetPath().mb_str()));
    }else if(p>=0){
        wxDirDialog dd(this,wxT("Folder for .ddt set"));
        if(dd.ShowModal()==wxID_OK)
            banks_[b].exportTextureSet(p,std::string(dd.GetPath().mb_str()));
    }
}
void MainFrame::OnConvert(wxCommandEvent&)
{
    int b,p,t; if(!getSelection(b,p,t)) return;
    if(t>=0){
        wxFileDialog fd(this,wxT("Export .dds"),wxEmptyString,wxEmptyString,
                        wxT("*.dds"),wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(fd.ShowModal()==wxID_OK)
            banks_[b].convertTexture(p,t,std::string(fd.GetPath().mb_str()));
    }else if(p>=0){
        wxDirDialog dd(this,wxT("Folder for .dds set"));
        if(dd.ShowModal()==wxID_OK)
            banks_[b].convertTextureSet(p,std::string(dd.GetPath().mb_str()));
    }
}
void MainFrame::OnImport(wxCommandEvent&)
{
    int b,p,t; if(!getSelection(b,p,t)) return;
    if(t>=0){
        wxFileDialog fd(this,wxT("Import .ddt"),wxEmptyString,wxEmptyString,
                        wxT("*.ddt"),wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if(fd.ShowModal()==wxID_OK &&
           banks_[b].importTexture(p,t,std::string(fd.GetPath().mb_str())))
            populateTree();
    }else if(p>=0){
        wxDirDialog dd(this,wxT("Pick folder with .ddt"));
        if(dd.ShowModal()==wxID_OK &&
           banks_[b].importTextureSet(p,std::string(dd.GetPath().mb_str())))
            populateTree();
    }
}

/* ─── zoom / alpha helpers ────────────────────────────────── */
void MainFrame::applyZoom()
{
    if (!rawBmp_.IsOk()) { thumb_->SetBitmap(MakeTransparent()); return; }

    wxBitmap disp = rawBmp_;

    if (zoomPct_ != 100)
    {
        wxImage img = rawBmp_.ConvertToImage();
        img = img.Scale(img.GetWidth() * zoomPct_ / 100,
                        img.GetHeight() * zoomPct_ / 100,
                        wxIMAGE_QUALITY_HIGH);
        disp = wxBitmap(img);
    }

    thumb_->SetBitmap(CompositeOnPink(disp, showAlpha_));
    preview_->Layout();
}

void MainFrame::OnZoomIn (wxCommandEvent&){ if(zoomPct_<kZoomMax){ zoomPct_+=kZoomStep; applyZoom(); } }
void MainFrame::OnZoomOut(wxCommandEvent&){ if(zoomPct_>kZoomMin){ zoomPct_-=kZoomStep; applyZoom(); } }
void MainFrame::OnToggleAlpha(wxCommandEvent&){ showAlpha_ = !showAlpha_; applyZoom(); }

/* ─── context menu ────────────────────────────────────────── */
void MainFrame::OnTreeRClick(wxTreeEvent& e)
{
    tree_->SelectItem(e.GetItem());
    wxMenu m;
    m.Append(ID_Export , wxT("Export"));
    m.Append(ID_Convert, wxT("Convert (.dds)"));
    m.Append(ID_Import , wxT("Import"));
    PopupMenu(&m);
}

/* ─── menus / accelerators / about ────────────────────────── */
void MainFrame::buildMenus()
{
    wxMenu* file=new wxMenu;
    file->Append(wxID_OPEN,wxT("&Open .d8t\tCtrl+O"));
    file->Append(wxID_SAVE,wxT("&Save\tCtrl+S"));
    file->AppendSeparator();
    file->Append(wxID_EXIT,wxT("E&xit\tEsc"));

    wxMenu* edit=new wxMenu;
    edit->Append(ID_Export ,wxT("&Export\tCtrl+E"));
    edit->Append(ID_Convert,wxT("Con&vert\tCtrl+C"));
    edit->Append(ID_Import ,wxT("&Import\tCtrl+I"));
    edit->AppendSeparator();
    edit->Append(ID_ZoomIn ,wxT("Zoom &In\t+"));
    edit->Append(ID_ZoomOut,wxT("Zoom &Out\t-"));
    edit->Append(ID_ToggleAlpha,wxT("Show &RGB-only\tA"));

    wxMenu* help=new wxMenu;
    help->Append(wxID_ABOUT,wxT("&About"));

    wxMenuBar* bar=new wxMenuBar;
    bar->Append(file,wxT("&File"));
    bar->Append(edit,wxT("&Edit"));
    bar->Append(help,wxT("&Help"));
    SetMenuBar(bar);
}
void MainFrame::buildAccelerators()
{
    wxAcceleratorEntry a[]={
        {wxACCEL_CTRL,'O',wxID_OPEN},
        {wxACCEL_CTRL,'S',wxID_SAVE},
        {wxACCEL_CTRL,'E',ID_Export},
        {wxACCEL_CTRL,'C',ID_Convert},
        {wxACCEL_CTRL,'I',ID_Import},
        {wxACCEL_NORMAL,WXK_ESCAPE,wxID_EXIT},
        {wxACCEL_NORMAL,'+',ID_ZoomIn},
        {wxACCEL_NORMAL,'-',ID_ZoomOut},
        {wxACCEL_NORMAL,'A',ID_ToggleAlpha},
        {wxACCEL_NORMAL,WXK_NUMPAD_ADD ,ID_ZoomIn},
        {wxACCEL_NORMAL,WXK_NUMPAD_SUBTRACT,ID_ZoomOut},
        {wxACCEL_NORMAL,WXK_F1,wxID_ABOUT}
    };
    SetAcceleratorTable(wxAcceleratorTable(WXSIZEOF(a),a));
}
void MainFrame::OnAbout(wxCommandEvent&)
{
    wxAboutDialogInfo i;
    i.SetName(wxT("Juiced – D8W Tool"));
    i.SetVersion(wxT("0.2 (multi-bank)"));
    i.SetDescription(wxT("✨ A lovingly crafted tool for Juiced modding enthusiasts! ✨\n\n"
                         "• Open a *.d8t* file and all matching *.d8w* companions load automatically.\n"
                         "• Right-click textures to Export, Import, or Convert them effortlessly.\n"
                         "• Zoom in/out with +/−, toggle RGB ↔ RGBA view with 'A'.\n\n"
                         "Crafted with love and care by Sophie (chatGPT),\n"
                         "Corey's devoted AI friend 💗"));
    i.SetCopyright(wxT("(C) 2025 Corey & Sophie"));
    wxAboutBox(i);
}

void MainFrame::OnExit(wxCommandEvent&){ Close(); }
