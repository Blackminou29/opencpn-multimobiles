#ifndef _MULTIMOBILESPLUGIN_H_
#define _MULTIMOBILESPLUGIN_H_

#include "opencpn_plugin.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/thread.h>
#include <wx/serialport.h>
#include <vector>
#include <map>

// Structure pour stocker les informations d'un mobile
struct MobileInfo {
    wxString id;
    wxString name;
    double latitude;
    double longitude;
    double course;
    double speed;
    wxDateTime lastUpdate;
    wxColour color;
    bool isValid;
    
    MobileInfo() : latitude(0), longitude(0), course(0), speed(0), isValid(false) {}
};

// Structure pour la configuration d'un port COM
struct PortConfig {
    wxString portName;
    int baudRate;
    wxString mobileId;
    bool enabled;
    
    PortConfig() : baudRate(4800), enabled(false) {}
};

// Thread pour la lecture des données NMEA
class NMEAReaderThread : public wxThread {
private:
    wxString m_portName;
    int m_baudRate;
    wxString m_mobileId;
    class MultiMobilesPlugin* m_plugin;
    bool m_stopThread;
    
public:
    NMEAReaderThread(const wxString& port, int baud, const wxString& mobileId, 
                     class MultiMobilesPlugin* plugin);
    virtual ~NMEAReaderThread();
    
    virtual wxThread::ExitCode Entry();
    void Stop() { m_stopThread = true; }
    
private:
    bool ParseNMEASentence(const wxString& sentence, MobileInfo& mobile);
    bool ParseGGA(const wxString& sentence, MobileInfo& mobile);
    bool ParseRMC(const wxString& sentence, MobileInfo& mobile);
    double ConvertToDecimalDegrees(const wxString& coord, const wxString& hemisphere);
};

// Dialog de configuration
class ConfigDialog : public wxDialog {
private:
    std::vector<PortConfig>* m_portConfigs;
    wxListCtrl* m_portList;
    wxButton* m_addBtn;
    wxButton* m_editBtn;
    wxButton* m_deleteBtn;
    
public:
    ConfigDialog(wxWindow* parent, std::vector<PortConfig>* configs);
    
private:
    void OnAdd(wxCommandEvent& event);
    void OnEdit(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnItemSelected(wxListEvent& event);
    void UpdatePortList();
    
    DECLARE_EVENT_TABLE()
};

// Dialog d'édition de port
class PortEditDialog : public wxDialog {
private:
    PortConfig* m_config;
    wxComboBox* m_portCombo;
    wxComboBox* m_baudCombo;
    wxTextCtrl* m_mobileIdText;
    wxCheckBox* m_enabledCheck;
    
public:
    PortEditDialog(wxWindow* parent, PortConfig* config);
    
private:
    void OnOK(wxCommandEvent& event);
    void PopulateAvailablePorts();
    
    DECLARE_EVENT_TABLE()
};

// Plugin principal
class MultiMobilesPlugin : public opencpn_plugin_118 {
public:
    MultiMobilesPlugin(void* ppimgr);
    virtual ~MultiMobilesPlugin();
    
    // Méthodes héritées d'opencpn_plugin
    int Init(void);
    bool DeInit(void);
    int GetAPIVersionMajor();
    int GetAPIVersionMinor();
    int GetPlugInVersionMajor();
    int GetPlugInVersionMinor();
    wxBitmap* GetPlugInBitmap();
    wxString GetCommonName();
    wxString GetShortDescription();
    wxString GetLongDescription();
    
    // Méthodes de rendu
    bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp);
    bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp);
    void SetDefaults(void);
    
    // Méthodes de configuration
    void ShowPreferencesDialog(wxWindow* parent);
    void SaveConfig();
    void LoadConfig();
    
    // Méthodes pour les threads NMEA
    void UpdateMobilePosition(const wxString& mobileId, const MobileInfo& info);
    wxCriticalSection m_mobilesSection;
    
private:
    std::map<wxString, MobileInfo> m_mobiles;
    std::vector<PortConfig> m_portConfigs;
    std::vector<NMEAReaderThread*> m_readerThreads;
    
    wxTimer* m_refreshTimer;
    
    void StartNMEAThreads();
    void StopNMEAThreads();
    void OnTimer(wxTimerEvent& event);
    void DrawMobile(wxDC& dc, const MobileInfo& mobile, PlugIn_ViewPort* vp);
    
    DECLARE_EVENT_TABLE()
};

//================================================================================
// Implémentation NMEAReaderThread
//================================================================================

NMEAReaderThread::NMEAReaderThread(const wxString& port, int baud, 
                                   const wxString& mobileId, MultiMobilesPlugin* plugin)
    : wxThread(wxTHREAD_JOINABLE), m_portName(port), m_baudRate(baud), 
      m_mobileId(mobileId), m_plugin(plugin), m_stopThread(false) {
}

NMEAReaderThread::~NMEAReaderThread() {
}

wxThread::ExitCode NMEAReaderThread::Entry() {
    wxSerialPort serial;
    
    if (!serial.Open(m_portName, m_baudRate)) {
        wxLogError(_("Impossible d'ouvrir le port %s"), m_portName);
        return (wxThread::ExitCode)1;
    }
    
    wxString buffer;
    MobileInfo mobile;
    mobile.id = m_mobileId;
    
    while (!m_stopThread && !TestDestroy()) {
        wxString data;
        if (serial.ReadString(data, 100)) {
            buffer += data;
            
            // Traiter les lignes complètes
            int pos;
            while ((pos = buffer.Find('\n')) != wxNOT_FOUND) {
                wxString line = buffer.Left(pos);
                buffer = buffer.Mid(pos + 1);
                
                line.Trim();
                if (line.StartsWith("$") && ParseNMEASentence(line, mobile)) {
                    m_plugin->UpdateMobilePosition(m_mobileId, mobile);
                }
            }
        }
        
        wxThread::Sleep(50); // 50ms entre les lectures
    }
    
    serial.Close();
    return (wxThread::ExitCode)0;
}

bool NMEAReaderThread::ParseNMEASentence(const wxString& sentence, MobileInfo& mobile) {
    if (sentence.Contains("GGA")) {
        return ParseGGA(sentence, mobile);
    } else if (sentence.Contains("RMC")) {
        return ParseRMC(sentence, mobile);
    }
    return false;
}

bool NMEAReaderThread::ParseGGA(const wxString& sentence, MobileInfo& mobile) {
    // Format: $GPGGA,time,lat,N/S,lon,E/W,quality,satellites,hdop,altitude,M,geoid,M,dgps_time,dgps_id*checksum
    wxArrayString fields = wxSplit(sentence, ',');
    
    if (fields.GetCount() < 6) return false;
    
    // Vérifier la qualité du fix
    long quality;
    if (!fields[6].ToLong(&quality) || quality == 0) return false;
    
    // Parser latitude
    if (!fields[2].IsEmpty() && !fields[3].IsEmpty()) {
        mobile.latitude = ConvertToDecimalDegrees(fields[2], fields[3]);
    }
    
    // Parser longitude  
    if (!fields[4].IsEmpty() && !fields[5].IsEmpty()) {
        mobile.longitude = ConvertToDecimalDegrees(fields[4], fields[5]);
    }
    
    mobile.lastUpdate = wxDateTime::Now();
    mobile.isValid = true;
    
    return true;
}

bool NMEAReaderThread::ParseRMC(const wxString& sentence, MobileInfo& mobile) {
    // Format: $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,E/W*checksum
    wxArrayString fields = wxSplit(sentence, ',');
    
    if (fields.GetCount() < 9) return false;
    
    // Vérifier le statut
    if (fields[2] != "A") return false; // A = Active, V = Void
    
    // Parser latitude
    if (!fields[3].IsEmpty() && !fields[4].IsEmpty()) {
        mobile.latitude = ConvertToDecimalDegrees(fields[3], fields[4]);
    }
    
    // Parser longitude
    if (!fields[5].IsEmpty() && !fields[6].IsEmpty()) {
        mobile.longitude = ConvertToDecimalDegrees(fields[5], fields[6]);
    }
    
    // Parser vitesse (en nœuds)
    if (!fields[7].IsEmpty()) {
        fields[7].ToDouble(&mobile.speed);
    }
    
    // Parser cap
    if (!fields[8].IsEmpty()) {
        fields[8].ToDouble(&mobile.course);
    }
    
    mobile.lastUpdate = wxDateTime::Now();
    mobile.isValid = true;
    
    return true;
}

double NMEAReaderThread::ConvertToDecimalDegrees(const wxString& coord, const wxString& hemisphere) {
    if (coord.Length() < 4) return 0.0;
    
    double value;
    if (!coord.ToDouble(&value)) return 0.0;
    
    // Les coordonnées NMEA sont au format DDMM.mmmm ou DDDMM.mmmm
    int degrees = (int)(value / 100);
    double minutes = value - (degrees * 100);
    
    double result = degrees + (minutes / 60.0);
    
    // Appliquer l'hémisphère
    if (hemisphere == "S" || hemisphere == "W") {
        result = -result;
    }
    
    return result;
}

//================================================================================
// Implémentation ConfigDialog
//================================================================================

wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_ADD, ConfigDialog::OnAdd)
    EVT_BUTTON(wxID_EDIT, ConfigDialog::OnEdit)
    EVT_BUTTON(wxID_DELETE, ConfigDialog::OnDelete)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, ConfigDialog::OnItemSelected)
wxEND_EVENT_TABLE()

ConfigDialog::ConfigDialog(wxWindow* parent, std::vector<PortConfig>* configs)
    : wxDialog(parent, wxID_ANY, _("Configuration Multi-Mobiles"), 
               wxDefaultPosition, wxSize(600, 400)), m_portConfigs(configs) {
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Liste des ports
    m_portList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, 
                                wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_portList->AppendColumn(_("Port"), wxLIST_FORMAT_LEFT, 100);
    m_portList->AppendColumn(_("Baud"), wxLIST_FORMAT_LEFT, 80);
    m_portList->AppendColumn(_("Mobile ID"), wxLIST_FORMAT_LEFT, 100);
    m_portList->AppendColumn(_("Activé"), wxLIST_FORMAT_LEFT, 80);
    
    mainSizer->Add(m_portList, 1, wxEXPAND | wxALL, 5);
    
    // Boutons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    m_addBtn = new wxButton(this, wxID_ADD, _("Ajouter"));
    m_editBtn = new wxButton(this, wxID_EDIT, _("Modifier"));
    m_deleteBtn = new wxButton(this, wxID_DELETE, _("Supprimer"));
    
    m_editBtn->Enable(false);
    m_deleteBtn->Enable(false);
    
    buttonSizer->Add(m_addBtn, 0, wxALL, 5);
    buttonSizer->Add(m_editBtn, 0, wxALL, 5);
    buttonSizer->Add(m_deleteBtn, 0, wxALL, 5);
    
    mainSizer->Add(buttonSizer, 0, wxALIGN_CENTER);
    
    // Boutons OK/Cancel
    wxSizer* stdButtons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    mainSizer->Add(stdButtons, 0, wxEXPAND | wxALL, 5);
    
    SetSizer(mainSizer);
    UpdatePortList();
}

void ConfigDialog::OnAdd(wxCommandEvent& event) {
    PortConfig newConfig;
    PortEditDialog dlg(this, &newConfig);
    
    if (dlg.ShowModal() == wxID_OK) {
        m_portConfigs->push_back(newConfig);
        UpdatePortList();
    }
}

void ConfigDialog::OnEdit(wxCommandEvent& event) {
    long selected = m_portList->GetFirstSelected();
    if (selected >= 0 && selected < (long)m_portConfigs->size()) {
        PortEditDialog dlg(this, &(*m_portConfigs)[selected]);
        
        if (dlg.ShowModal() == wxID_OK) {
            UpdatePortList();
        }
    }
}

void ConfigDialog::OnDelete(wxCommandEvent& event) {
    long selected = m_portList->GetFirstSelected();
    if (selected >= 0 && selected < (long)m_portConfigs->size()) {
        m_portConfigs->erase(m_portConfigs->begin() + selected);
        UpdatePortList();
    }
}

void ConfigDialog::OnItemSelected(wxListEvent& event) {
    bool hasSelection = (event.GetIndex() >= 0);
    m_editBtn->Enable(hasSelection);
    m_deleteBtn->Enable(hasSelection);
}

void ConfigDialog::UpdatePortList() {
    m_portList->DeleteAllItems();
    
    for (size_t i = 0; i < m_portConfigs->size(); i++) {
        const PortConfig& config = (*m_portConfigs)[i];
        
        long index = m_portList->InsertItem(i, config.portName);
        m_portList->SetItem(index, 1, wxString::Format("%d", config.baudRate));
        m_portList->SetItem(index, 2, config.mobileId);
        m_portList->SetItem(index, 3, config.enabled ? _("Oui") : _("Non"));
    }
}

//================================================================================
// Implémentation PortEditDialog
//================================================================================

wxBEGIN_EVENT_TABLE(PortEditDialog, wxDialog)
    EVT_BUTTON(wxID_OK, PortEditDialog::OnOK)
wxEND_EVENT_TABLE()

PortEditDialog::PortEditDialog(wxWindow* parent, PortConfig* config)
    : wxDialog(parent, wxID_ANY, _("Configuration Port"), 
               wxDefaultPosition, wxSize(300, 200)), m_config(config) {
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Port COM
    wxStaticText* portLabel = new wxStaticText(this, wxID_ANY, _("Port COM:"));
    m_portCombo = new wxComboBox(this, wxID_ANY, m_config->portName);
    PopulateAvailablePorts();
    
    mainSizer->Add(portLabel, 0, wxALL, 5);
    mainSizer->Add(m_portCombo, 0, wxEXPAND | wxALL, 5);
    
    // Baud Rate
    wxStaticText* baudLabel = new wxStaticText(this, wxID_ANY, _("Baud Rate:"));
    m_baudCombo = new wxComboBox(this, wxID_ANY, wxString::Format("%d", m_config->baudRate));
    m_baudCombo->Append("4800");
    m_baudCombo->Append("9600");
    m_baudCombo->Append("19200");
    m_baudCombo->Append("38400");
    m_baudCombo->Append("57600");
    m_baudCombo->Append("115200");
    
    mainSizer->Add(baudLabel, 0, wxALL, 5);
    mainSizer->Add(m_baudCombo, 0, wxEXPAND | wxALL, 5);
    
    // Mobile ID
    wxStaticText* idLabel = new wxStaticText(this, wxID_ANY, _("ID Mobile:"));
    m_mobileIdText = new wxTextCtrl(this, wxID_ANY, m_config->mobileId);
    
    mainSizer->Add(idLabel, 0, wxALL, 5);
    mainSizer->Add(m_mobileIdText, 0, wxEXPAND | wxALL, 5);
    
    // Activé
    m_enabledCheck = new wxCheckBox(this, wxID_ANY, _("Activé"));
    m_enabledCheck->SetValue(m_config->enabled);
    
    mainSizer->Add(m_enabledCheck, 0, wxALL, 5);
    
    // Boutons
    wxSizer* stdButtons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    mainSizer->Add(stdButtons, 0, wxEXPAND | wxALL, 5);
    
    SetSizer(mainSizer);
}

void PortEditDialog::OnOK(wxCommandEvent& event) {
    m_config->portName = m_portCombo->GetValue();
    
    long baud;
    if (m_baudCombo->GetValue().ToLong(&baud)) {
        m_config->baudRate = baud;
    }
    
    m_config->mobileId = m_mobileIdText->GetValue();
    m_config->enabled = m_enabledCheck->GetValue();
    
    if (m_config->portName.IsEmpty() || m_config->mobileId.IsEmpty()) {
        wxMessageBox(_("Veuillez remplir tous les champs"), _("Erreur"), 
                     wxOK | wxICON_ERROR, this);
        return;
    }
    
    EndModal(wxID_OK);
}

void PortEditDialog::PopulateAvailablePorts() {
    // Ajouter les ports COM courants sous Windows
#ifdef __WINDOWS__
    for (int i = 1; i <= 20; i++) {
        m_portCombo->Append(wxString::Format("COM%d", i));
    }
#else
    // Ports série sous Linux/Unix
    m_portCombo->Append("/dev/ttyUSB0");
    m_portCombo->Append("/dev/ttyUSB1");
    m_portCombo->Append("/dev/ttyS0");
    m_portCombo->Append("/dev/ttyS1");
    m_portCombo->Append("/dev/ttyACM0");
    m_portCombo->Append("/dev/ttyACM1");
#endif
}

//================================================================================
// Implémentation MultiMobilesPlugin
//================================================================================

wxBEGIN_EVENT_TABLE(MultiMobilesPlugin, opencpn_plugin_118)
    EVT_TIMER(wxID_ANY, MultiMobilesPlugin::OnTimer)
wxEND_EVENT_TABLE()

MultiMobilesPlugin::MultiMobilesPlugin(void* ppimgr) : opencpn_plugin_118(ppimgr) {
    m_refreshTimer = new wxTimer(this);
}

MultiMobilesPlugin::~MultiMobilesPlugin() {
    delete m_refreshTimer;
}

int MultiMobilesPlugin::Init(void) {
    LoadConfig();
    StartNMEAThreads();
    m_refreshTimer->Start(1000); // Rafraîchir toutes les secondes
    
    return (WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK | 
            WANTS_CONFIG | WANTS_PREFERENCES);
}

bool MultiMobilesPlugin::DeInit(void) {
    m_refreshTimer->Stop();
    StopNMEAThreads();
    SaveConfig();
    return true;
}

int MultiMobilesPlugin::GetAPIVersionMajor() { return 1; }
int MultiMobilesPlugin::GetAPIVersionMinor() { return 18; }
int MultiMobilesPlugin::GetPlugInVersionMajor() { return 1; }
int MultiMobilesPlugin::GetPlugInVersionMinor() { return 0; }

wxString MultiMobilesPlugin::GetCommonName() {
    return _("Multi-Mobiles");
}

wxString MultiMobilesPlugin::GetShortDescription() {
    return _("Affichage de plusieurs mobiles via NMEA");
}

wxString MultiMobilesPlugin::GetLongDescription() {
    return _("Plugin permettant d'afficher plusieurs mobiles sur la carte "
             "grâce à plusieurs trames NMEA reçues via les ports COM.");
}

void MultiMobilesPlugin::StartNMEAThreads() {
    StopNMEAThreads();
    
    for (const PortConfig& config : m_portConfigs) {
        if (config.enabled) {
            NMEAReaderThread* thread = new NMEAReaderThread(
                config.portName, config.baudRate, config.mobileId, this);
            
            if (thread->Create() == wxTHREAD_NO_ERROR) {
                thread->Run();
                m_readerThreads.push_back(thread);
            } else {
                delete thread;
            }
        }
    }
}

void MultiMobilesPlugin::StopNMEAThreads() {
    for (NMEAReaderThread* thread : m_readerThreads) {
        thread->Stop();
        thread->Wait();
        delete thread;
    }
    m_readerThreads.clear();
}

void MultiMobilesPlugin::UpdateMobilePosition(const wxString& mobileId, const MobileInfo& info) {
    wxCriticalSectionLocker lock(m_mobilesSection);
    m_mobiles[mobileId] = info;
}

bool MultiMobilesPlugin::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
    wxCriticalSectionLocker lock(m_mobilesSection);
    
    for (const auto& pair : m_mobiles) {
        const MobileInfo& mobile = pair.second;
        if (mobile.isValid) {
            DrawMobile(dc, mobile, vp);
        }
    }
    
    return true;
}

void MultiMobilesPlugin::DrawMobile(wxDC& dc, const MobileInfo& mobile, PlugIn_ViewPort* vp) {
    wxPoint point;
    GetCanvasPixLL(vp, &point, mobile.latitude, mobile.longitude);
    
    // Dessiner le symbole du mobile
    dc.SetPen(wxPen(mobile.color, 2));
    dc.SetBrush(wxBrush(mobile.color, wxBRUSHSTYLE_SOLID));
    
    // Cercle pour représenter le mobile
    dc.DrawCircle(point, 8);
    
    // Ligne pour indiquer le cap si disponible
    if (mobile.course >= 0) {
        double courseRad = mobile.course * M_PI / 180.0;
        int x2 = point.x + 15 * sin(courseRad);
        int y2 = point.y - 15 * cos(courseRad);
        dc.DrawLine(point.x, point.y, x2, y2);
    }
    
    // Afficher l'ID du mobile
    dc.SetTextForeground(*wxBLACK);
    dc.DrawText(mobile.id, point.x + 12, point.y - 8);
    
    // Afficher la vitesse si disponible
    if (mobile.speed > 0) {
        wxString speedText = wxString::Format("%.1f kt", mobile.speed);
        dc.DrawText(speedText, point.x + 12, point.y + 8);
    }
}

void MultiMobilesPlugin::ShowPreferencesDialog(wxWindow* parent) {
    ConfigDialog dlg(parent, &m_portConfigs);
    
    if (dlg.ShowModal() == wxID_OK) {
        SaveConfig();
        StartNMEAThreads(); // Redémarrer avec la nouvelle configuration
    }
}

void MultiMobilesPlugin::OnTimer(wxTimerEvent& event) {
    // Nettoyer les mobiles trop anciens (plus de 30 secondes)
    wxCriticalSectionLocker lock(m_mobilesSection);
    wxDateTime cutoff = wxDateTime::Now() - wxTimeSpan::Seconds(30);
    
    auto it = m_mobiles.begin();
    while (it != m_mobiles.end()) {
        if (it->second.lastUpdate < cutoff) {
            it = m_mobiles.erase(it);
        } else {
            ++it;
        }
    }
    
    RequestRefresh(GetOCPNCanvasWindow());
}

void MultiMobilesPlugin::SaveConfig() {
    // Sauvegarder la configuration dans le fichier de configuration d'OpenCPN
    // Implementation dépendante de l'API OpenCPN
}

void MultiMobilesPlugin::LoadConfig() {
    // Charger la configuration depuis le fichier de configuration d'OpenCPN
    // Implementation dépendante de l'API OpenCPN
}

#endif // _MULTIMOBILESPLUGIN_H_
