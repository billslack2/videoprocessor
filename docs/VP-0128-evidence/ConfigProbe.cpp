#define main VP0128ExistingTestMain
#include "../../src/VideoProcessor-ConfigTests/ConfigEditorWindowTests.cpp"
#undef main

void dumpControl(ConfigEditorWindow& w, const char* name) {
    auto* o = w.findChild<QWidget*>(name);
    if (!o) { std::cout << name << " MISSING\n"; return; }
    QString display, data;
    if (auto* c=qobject_cast<QComboBox*>(o)) { display=c->currentText(); data=c->currentData().toString(); }
    if (auto* c=qobject_cast<QLineEdit*>(o)) { display=c->text(); data=c->hasAcceptableInput()?"acceptable":"INVALID"; }
    if (auto* c=qobject_cast<QCheckBox*>(o)) display=c->isChecked()?"checked":"unchecked";
    if (auto* c=qobject_cast<QLabel*>(o)) display=c->text();
    std::cout << name << " display=" << display.toStdString() << " data=" << data.toStdString() << " effective=" << o->property("effectiveValue").toString().toStdString() << "\n";
}
void dumpQuality(ConfigEditorWindow& w) {
    for (const char* n : {"config.vprenderer.quality", "config.vprenderer.peak_detection.auto_status", "config.vprenderer.contrast_recovery.auto_status", "config.vprenderer.scaling.upscaler.auto_status", "config.vprenderer.scaling.downscaler.auto_status", "config.vprenderer.scaling.sigmoid.auto_status"}) dumpControl(w,n);
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    for (const char* q : {"fast","balanced","high"}) {
        QTemporaryDir dir; auto path=dir.filePath("VideoProcessor.cfg"); QFile f(path); f.open(QIODevice::WriteOnly);
        f.write(QByteArray("[vprenderer.Default]\nquality: ")+q+"\n[vprenderer.Child]\ntone_mapping: reinhard\n[vprenderer.scaling.Default]\nupscaler: auto\ndownscaler: auto\nsigmoid: auto\n[vprenderer.color.Default]\noutput_range: limited\noutput_transport_gamma: auto\n[vprenderer.color.Child]\n"); f.close();
        ConfigEditorWindow w(path,0,true); w.setActiveProfileStatusForTesting({}, {}, {}, {}, {}); std::cout << "QUALITY " << q << " BASE\n"; dumpQuality(w);
        requireControl<QListWidget>(w,"config.vprenderer.profiles")->setCurrentRow(1); QCoreApplication::processEvents();
        std::cout << "QUALITY " << q << " INHERITED CHILD\n"; dumpQuality(w);
        requireControl<QListWidget>(w,"config.vprenderer.color.profiles")->setCurrentRow(1); QCoreApplication::processEvents();
        dumpControl(w,"config.vprenderer.color.output_range"); dumpControl(w,"config.vprenderer.color.output_transport_gamma.auto_status");
    }
    for (const char* boolean : {"true","on","yes","1","false"}) {
        QTemporaryDir dir; auto path=dir.filePath("VideoProcessor.cfg"); QFile f(path); f.open(QIODevice::WriteOnly);
        f.write(QByteArray("[vprenderer.Default]\n[vprenderer.color.Default]\noutput_diagnostics: ")+boolean+"\nreport_bt2020_to_display: "+boolean+"\n[vprenderer.zoom.Default]\nautomatic_crop: "+boolean+"\nsubtitle_fit: "+boolean+"\n"); f.close();
        ConfigFile cfg; cfg.Load(path.toStdString()); RendererProfileConfig::Model model; std::string error;
        std::cout << "BOOLEAN " << boolean << " parser=" << RendererProfileConfig::Read(cfg,model,error) << " error=" << error << "\n";
        ConfigEditorWindow w(path,0,true); w.setActiveProfileStatusForTesting({}, {}, {}, {}, {});
        for(const char* n:{"config.vprenderer.color.output_diagnostics","config.vprenderer.color.report_bt2020_to_display","config.vprenderer.zoom.automatic_crop","config.vprenderer.zoom.subtitle_fit"}) dumpControl(w,n);
    }
    for (const char* type : {"missing","empty","minimal","sample"}) {
        QTemporaryDir dir; auto path=dir.filePath("VideoProcessor.cfg");
        if(std::string(type)=="sample") QFile::copy(repositoryPath("VideoProcessor.cfg"),path);
        else if(std::string(type)!="missing") { QFile f(path); f.open(QIODevice::WriteOnly); if(std::string(type)=="minimal") f.write("[vprenderer.Default]\n"); }
        ConfigEditorWindow w(path,0,true); w.setActiveProfileStatusForTesting({}, {}, {}, {}, {}); std::cout << "INITIAL " << type << "\n";
        for(const char* n:{"config.vprenderer.sdr_target_nits","config.vprenderer.sdr_black_nits","config.vprenderer.display_bit_depth","config.vprenderer.color.output_gamma","config.vprenderer.scaling.upscaler","configurationStatus"}) dumpControl(w,n);
    }
    for(const char* value:{"ewa_lanczos","none","typo_filter"}) {
        QTemporaryDir dir; auto path=dir.filePath("VideoProcessor.cfg"); QFile f(path); f.open(QIODevice::WriteOnly); f.write(QByteArray("[vprenderer.Default]\n[vprenderer.scaling.Default]\ndownscaler: ")+value+"\n"); f.close();
        ConfigFile cfg; cfg.Load(path.toStdString()); RendererProfileConfig::Model model; std::string error;
        std::cout << "DOWNSCALER " << value << " parser=" << RendererProfileConfig::Read(cfg,model,error) << " error=" << error << "\n";
        ConfigEditorWindow w(path,0,true); w.setActiveProfileStatusForTesting({}, {}, {}, {}, {}); dumpControl(w,"config.vprenderer.scaling.downscaler"); dumpControl(w,"configurationStatus");
        auto* apply=requireControl<QPushButton>(w,"applyConfiguration"); std::cout << "save_enabled=" << apply->isEnabled() << "\n";
        if(apply->isEnabled()) { apply->click(); QCoreApplication::processEvents(); std::cout << "saved_still_contains_value=" << readBytes(path).contains(value) << "\n"; }
    }
    return 0;
}