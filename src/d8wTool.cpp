#include "d8wTool.h"
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>

/*──────────────────────── event table ───────────────────────*/
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU (wxID_OPEN,   MainFrame::OnOpen)
    EVT_MENU (wxID_SAVE,   MainFrame::OnSave)
    EVT_MENU (wxID_EXIT,   MainFrame::OnExit)

    EVT_MENU (ID_Export,   MainFrame::OnExport)
    EVT_MENU (ID_Convert,  MainFrame::OnConvert)
    EVT_MENU (ID_Import,   MainFrame::OnImport)

    EVT_MENU (ID_ZoomIn,   MainFrame::OnZoomIn)
    EVT_MENU (ID_ZoomOut,  MainFrame::OnZoomOut)

    EVT_MENU (wxID_ABOUT,  MainFrame::OnAbout)

    EVT_TREE_SEL_CHANGED (ID_Tree, MainFrame::OnSelChanged)
    EVT_TREE_ITEM_RIGHT_CLICK(ID_Tree, MainFrame::OnTreeRClick)   // NEW
wxEND_EVENT_TABLE()

/*──────────────────────── helpers ───────────────────────────*/
static wxBitmap MakeTransparentBmp(int w=1,int h=1)
{
    wxImage img(w,h,true); img.InitAlpha();  *img.GetAlpha() = 0;
    return wxBitmap(img);
}
static wxBitmap CompositeOnPink(const wxBitmap& src)
{
    if(!src.IsOk()) return src;
    wxBitmap dst(src.GetWidth(),src.GetHeight(),24);
    wxMemoryDC dc(dst);
    dc.SetBackground(wxBrush(wxColour(255,0,255)));
    dc.Clear();
    dc.DrawBitmap(src,0,0,true);
    dc.SelectObject(wxNullBitmap);
    return dst;
}
static wxString TempDDS(){ wxFileName t=wxFileName::CreateTempFileName("d8w"); t.SetExt("dds"); return t.GetFullPath(); }

/*──────────────────────── application ───────────────────────*/
bool d8wToolApp::OnInit()
{
    auto* f = new MainFrame("Juiced: D8W Tool");
    f->Show();
    return true;
}

/*──────────────────────── constructor ───────────────────────*/
MainFrame::MainFrame(const wxString& title)
: wxFrame(nullptr,wxID_ANY,title,wxDefaultPosition,wxSize(900,600)),
  zoomPct_(100)
{
    SetIcon( wxICON(APP_ICON) );                               // resource "app.ico"





    buildMenus();  buildAccelerators();

    splitter_ = new wxSplitterWindow(this,wxID_ANY);
    tree_     = new wxTreeCtrl(splitter_,ID_Tree,
                               wxDefaultPosition,wxDefaultSize,
                               wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT);

    preview_  = new wxPanel(splitter_,wxID_ANY);
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
    infoText_ = new wxStaticText(preview_,wxID_ANY,"No file loaded");
    vbox->Add(infoText_,0,wxALL|wxEXPAND,5);
    thumb_    = new wxStaticBitmap(preview_,wxID_ANY,MakeTransparentBmp());
    vbox->Add(thumb_,0,wxALL,5);
    preview_->SetSizer(vbox);

    splitter_->SplitVertically(tree_,preview_,300);
    splitter_->SetMinimumPaneSize(200);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
    root->Add(splitter_,1,wxEXPAND);
    SetSizer(root); Centre();
}

/*──────────────────────── menu / accel build ───────────────*/
void MainFrame::buildMenus()
{
    wxMenu* file = new wxMenu;
    file->Append(wxID_OPEN,"&Open...\tCtrl+O");
    file->Append(wxID_SAVE,"&Save\tCtrl+S");
    file->AppendSeparator(); file->Append(wxID_EXIT,"E&xit\tEsc");

    wxMenu* edit = new wxMenu;
    edit->Append(ID_Export, "&Export\tCtrl+E");
    edit->Append(ID_Convert,"Con&vert\tCtrl+C");
    edit->Append(ID_Import, "&Import\tCtrl+I");
    edit->AppendSeparator();
    edit->Append(ID_ZoomIn, "Zoom &In\t+");
    edit->Append(ID_ZoomOut,"Zoom &Out\t-");

    wxMenu* help = new wxMenu;
    help->Append(wxID_ABOUT,"&About");

    auto* bar=new wxMenuBar;
    bar->Append(file,"&File"); bar->Append(edit,"&Edit"); bar->Append(help,"&Help");
    SetMenuBar(bar);
}
void MainFrame::buildAccelerators()
{
    wxAcceleratorEntry e[]={
        {wxACCEL_CTRL,'O',wxID_OPEN}, {wxACCEL_CTRL,'S',wxID_SAVE},
        {wxACCEL_CTRL,'E',ID_Export}, {wxACCEL_CTRL,'C',ID_Convert},
        {wxACCEL_CTRL,'I',ID_Import},
        {wxACCEL_NORMAL,WXK_ESCAPE,wxID_EXIT},
        {wxACCEL_NORMAL,'+',ID_ZoomIn}, {wxACCEL_NORMAL,'-',ID_ZoomOut},
        {wxACCEL_NORMAL,WXK_NUMPAD_ADD,ID_ZoomIn},
        {wxACCEL_NORMAL,WXK_NUMPAD_SUBTRACT,ID_ZoomOut},
        {wxACCEL_NORMAL,WXK_F1,wxID_ABOUT}
    };
    SetAcceleratorTable(wxAcceleratorTable(WXSIZEOF(e),e));
}

/*──────────────────────── file I/O ──────────────────────────*/
void MainFrame::OnOpen(wxCommandEvent&)
{
    wxFileDialog d(this,"Open .d8w","","","d8w files (*.d8w)|*.d8w",
                   wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if(d.ShowModal()!=wxID_OK) return;

    curW_=d.GetPath(); bank_=juiced::D8WFile();
    if(!bank_.load(std::string(curW_.mb_str()))){
        wxMessageBox("Failed to load","Error",wxICON_ERROR); curW_.clear(); return;
    }
    curT_=wxFileName(curW_).GetPathWithSep()+wxFileName(curW_).GetName()+".d8t";
    zoomPct_=100; rawBmp_=wxBitmap();
    populateTree(); updateTitle();
}
void MainFrame::OnSave(wxCommandEvent&)
{
    if(!bank_.isDirty()){wxBell(); return;}
    wxString outT=wxFileName(curW_).GetPath()+"\\"+wxFileName(curW_).GetName()+".d8t";
    if(!bank_.save(std::string(curW_.mb_str()),std::string(outT.mb_str())))
        wxMessageBox("Save failed","Error",wxICON_ERROR);
    updateTitle();
}

void MainFrame::OnExit(wxCommandEvent&)          // <── missing earlier
{
    Close();                                     // normal frame shutdown
}

/*──────────────────────── context helpers ──────────────────*/
bool MainFrame::getSelection(int& p,int& t) const
{
    auto id=tree_->GetSelection(); if(!id.IsOk()) return false;
    auto* d=(TexItemData*)tree_->GetItemData(id); if(!d) return false;
    p=d->pack; t=d->tex; return true;
}

/*──────────────────────── export/convert/import ────────────*/
void MainFrame::OnExport(wxCommandEvent&)
{
    int p,t; if(!getSelection(p,t)) return;
    if(t>=0){
        wxFileDialog dlg(this,"Export .ddt","","","*.ddt",
                         wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(dlg.ShowModal()==wxID_OK)
            bank_.exportTexture(p,t,std::string(dlg.GetPath().mb_str()));
    }else if(p>=0){
        wxDirDialog dlg(this,"Choose folder for .ddt set");
        if(dlg.ShowModal()==wxID_OK)
            bank_.exportTextureSet(p,std::string(dlg.GetPath().mb_str()));
    }
}
void MainFrame::OnConvert(wxCommandEvent&)
{
    int p,t; if(!getSelection(p,t)) return;
    if(t>=0){
        wxFileDialog dlg(this,"Export .dds","","","*.dds",
                         wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(dlg.ShowModal()==wxID_OK)
            bank_.convertTexture(p,t,std::string(dlg.GetPath().mb_str()));
    }else if(p>=0){
        wxDirDialog dlg(this,"Choose folder for .dds set");
        if(dlg.ShowModal()==wxID_OK)
            bank_.convertTextureSet(p,std::string(dlg.GetPath().mb_str()));
    }
}
void MainFrame::OnImport(wxCommandEvent&)
{
    int p,t; if(!getSelection(p,t)) return;
    if(t>=0){
        wxFileDialog dlg(this,"Import .ddt","","","*.ddt",
                         wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if(dlg.ShowModal()==wxID_OK &&
           bank_.importTexture(p,t,std::string(dlg.GetPath().mb_str())))
            populateTree();
    }else if(p>=0){
        wxDirDialog dlg(this,"Select folder with .ddt set");
        if(dlg.ShowModal()==wxID_OK &&
           bank_.importTextureSet(p,std::string(dlg.GetPath().mb_str())))
            populateTree();
    }
}

/*──────────────────────── zoom ─────────────────────────────*/
void MainFrame::OnZoomIn(wxCommandEvent&){ if(zoomPct_<kZoomMax){zoomPct_+=kZoomStep; applyZoom();}}
void MainFrame::OnZoomOut(wxCommandEvent&){if(zoomPct_>kZoomMin){zoomPct_-=kZoomStep; applyZoom();}}

/*──────────────────────── about ────────────────────────────*/
void MainFrame::OnAbout(wxCommandEvent&)
{
    wxAboutDialogInfo i;
    i.SetName("d8wTool"); i.SetVersion("0.1");
    i.SetDescription("Right-click tree for Export / Import / Convert.\nZoom with + / -");
    wxAboutBox(i);
}

/*──────────────────────── tree build ───────────────────────*/
void MainFrame::clearTree(){ tree_->DeleteAllItems(); }
void MainFrame::populateTree()
{
    clearTree();
    if(curW_.IsEmpty()){ tree_->AddRoot("No file"); return; }
    auto root=tree_->AddRoot(wxFileName(curW_).GetFullName());
    tree_->SetItemData(root,new TexItemData(-2,-1));

    for(size_t p=0;p<bank_.texturePackCount();++p){
        auto pack=tree_->AppendItem(root,wxString::Format("TexSet%zu",p));
        tree_->SetItemData(pack,new TexItemData((int)p,-1));

        for(size_t i=0;i<bank_.textureCount(p);++i){
            auto id=tree_->AppendItem(pack,wxString::Format("Tex%zu%04zu",p,i));
            tree_->SetItemData(id,new TexItemData((int)p,(int)i));
            if(bank_.isTextureModified(p,i)) tree_->SetItemTextColour(id,*wxRED);
        }
    }
    tree_->Expand(root);
}

/*──────────────────────── selection change ─────────────────*/
void MainFrame::OnSelChanged(wxTreeEvent& e)
{
    int p,t; if(!getSelection(p,t)) return;
    if(t>=0) showTextureInfo(p,t);
    else if(p>=0) showPackInfo(p);
}

/*──────────────────────── pack info ───────────────────────*/
void MainFrame::showPackInfo(int p)
{
    zoomPct_=100; rawBmp_=wxBitmap();
    infoText_->SetLabel(wxString::Format("Texture pack %d\nTextures: %zu",p,
                                         bank_.textureCount(p)));
    thumb_->SetBitmap(MakeTransparentBmp());
    preview_->Layout();
}

/*──────────────────────── texture info ────────────────────*/
/* helper: pad / truncate to fixed width (18) */
static wxString col(const wxString& s)
{
    wxString out = s;
    if(out.Length() < 18)        out.Pad(18 - out.Length(), ' ');
    else if(out.Length() > 18)   out = out.Left(18);
    return out;
}

void MainFrame::showTextureInfo(int pack,int tex)
{
    const auto& h = bank_.texture(pack, tex);

    /* decode FourCC → "DXT1" / "DXT5" / etc. */
    char cc[5] = { char(h.type&0xFF),
                   char((h.type>>8)&0xFF),
                   char((h.type>>16)&0xFF),
                   char((h.type>>24)&0xFF), 0 };
    wxString fcc = wxString::FromUTF8(cc);

    /* build the 4×3 table */
    wxString row0 = col(wxString::Format("Tex%04d", tex)) +
                    col(wxString::Format("%ux%u", h.width, h.height)) +
                    col(wxString::Format("Size:%u", h.size));

    wxString row1 = col(wxString::Format("Type:%s", fcc)) +
                    col(wxString::Format("Mips:%u", h.mipCnt)) +
                    col(wxString::Format("u07:%u", h.unk07));

    wxString row2 = col(wxString::Format("u08:%u", h.unk08)) +
                    col(wxString::Format("u09:%u", h.unk09)) +
                    col(wxString::Format("u10:%u", h.unk10));

    wxString row3 = col(wxString::Format("u11:%u",   h.unk11)) +
                    col(wxString::Format("u12:%.2f", h.unk12)) +
                    col(wxString::Format("u13:%.2f", h.unk13));

    infoText_->SetLabel(row0 + "\n" + row1 + "\n" + row2 + "\n" + row3);

    /* thumbnail refresh unchanged -------------------------------- */
    rawBmp_ = wxBitmap();   // clears it                     // clear previous
    zoomPct_ = 100;
    wxString tmp = TempDDS();
    if (bank_.convertTexture(pack, tex, std::string(tmp.mb_str())))
    {
        DDSImage img;
        if (img.LoadFromFile(tmp))
            rawBmp_ = img.AsBitmap(0, true);   // full size + alpha
        wxRemoveFile(tmp);
    }
    if (!rawBmp_.IsOk())
        rawBmp_ = MakeTransparentBmp();

    applyZoom();
}

/*──────────────────────── zoom apply ──────────────────────*/
void MainFrame::applyZoom()
{
    if(!rawBmp_.IsOk()){ thumb_->SetBitmap(MakeTransparentBmp()); return; }

    wxBitmap disp=rawBmp_;
    if(zoomPct_!=100){
        wxImage img=rawBmp_.ConvertToImage();
        img=img.Scale(img.GetWidth()*zoomPct_/100,
                      img.GetHeight()*zoomPct_/100,
                      wxIMAGE_QUALITY_HIGH);
        disp=wxBitmap(img);
    }
    thumb_->SetBitmap(CompositeOnPink(disp)); preview_->Layout();
}

/*──────────────────────── right-click menu ────────────────*/
void MainFrame::OnTreeRClick(wxTreeEvent& evt)
{
    tree_->SelectItem(evt.GetItem());
    int p,t; if(!getSelection(p,t)) return;

    wxMenu m;
    m.Append(ID_Export,  "Export");
    m.Append(ID_Convert, "Convert");
    m.Append(ID_Import,  "Import");
    PopupMenu(&m);
}

/*──────────────────────── title update ────────────────────*/
void MainFrame::updateTitle()
{
    wxString t="Juiced: D8W Tool";
    if(!curW_.IsEmpty()){
        t += " - " + wxFileName(curW_).GetFullName();
        t += " / " + wxFileName(curT_).GetFullName();
    }
    if(bank_.isDirty()) t += " *";
    SetTitle(t);
}
