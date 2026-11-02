#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSaveFile>
#include <QStackedWidget>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>

#include <cstdio>
#include <map>
#include <stdexcept>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
QByteArray readAsset(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 4 * 1024 * 1024)
        throw std::runtime_error("A required bounded seasonal test asset could not be read.");
    return file.readAll();
}

class Page final : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
    QJsonArray console;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int line,
                                  const QString& source) override {
        console.append(QJsonObject{{"level", int(level)}, {"message", message}, {"line", line}, {"source", source}});
    }
};

class Requests final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto url = info.requestUrl();
        if ((url.scheme() != "drifter" || url.host() != "app") && url.scheme() != "data") info.block(true);
    }
};

class Assets final : public QWebEngineUrlSchemeHandler {
    std::map<QString, QByteArray> files_;

public:
    Assets(std::map<QString, QByteArray> files, QObject* parent)
        : QWebEngineUrlSchemeHandler(parent), files_(std::move(files)) {}
    void requestStarted(QWebEngineUrlRequestJob* job) override {
        const auto url = job->requestUrl();
        const auto found = files_.find(url.path());
        if (job->requestMethod() != "GET" || url.host() != "app" || !url.query().isEmpty() || found == files_.end()) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        const auto path = url.path();
        const QByteArray mime = path.endsWith(".js")    ? "application/javascript"
                                : path.endsWith(".css") ? "text/css"
                                : path.endsWith(".svg") ? "image/svg+xml"
                                                        : "text/html";
        auto* buffer = new QBuffer(job);
        buffer->setData(found->second);
        buffer->open(QIODevice::ReadOnly);
        job->reply(mime, buffer);
    }
};

const char Html[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'; frame-src 'none'; base-uri 'none'; form-action 'none'">
<title>Seasonal rendering regression</title><link rel="stylesheet" href="style.css"><link rel="stylesheet" href="test.css"><script src="test.js" defer></script></head>
<body><section class="samples"><h1>Seasonal artwork · native Qt</h1><p>All eighteen local glyphs and the production falling animation.</p><div class="samples-glyphs"></div></section><div class="seasonal-effects"></div></body></html>)HTML";

const char Css[] = ".samples{padding:64px}.samples-glyphs{display:flex;flex-wrap:wrap;gap:16px;font-size:64px}"
                   "body{background:#0d141a;color:#e6e9eb}";

const char Script[] = R"JS(
const codes=[0x1f386,0x1f387,0x2728,0x1f383,0x1f47b,0x1f987,0x1f342,0x2744,0x2603,0x1f332,0x1f338,0x1f337,0x1f343,0x2600,0x1f33b,0x1f33f,0x1f341,0x1f330];
const glyph=code=>`<span class="seasonal-glyph glyph-u${code.toString(16)}">${String.fromCodePoint(code)}</span>`;
document.querySelector('.samples-glyphs').innerHTML=codes.map(glyph).join('');
document.querySelector('.seasonal-effects').innerHTML=codes.map((code,i)=>`<span class="seasonal-particle emoji-${i+1}">${glyph(code)}</span>`).join('');
window.spriteProof={state:'loading'};
const image=new Image();
image.onload=()=>{try{
  const canvas=document.createElement('canvas');canvas.width=768;canvas.height=384;
  const context=canvas.getContext('2d',{willReadFrequently:true});context.drawImage(image,0,0);
  const cells=[];
  for(let row=0;row<3;row++)for(let col=0;col<6;col++){
    const bytes=context.getImageData(col*128,row*128,128,128).data;let pixels=0;const colors=new Set();
    for(let i=0;i<bytes.length;i+=4)if(bytes[i+3]){pixels++;colors.add(bytes[i]+','+bytes[i+1]+','+bytes[i+2]);}
    cells.push({row,col,pixels,colors:colors.size});
  }
  window.spriteProof={state:'loaded',width:image.naturalWidth,height:image.naturalHeight,cells};
}catch(error){window.spriteProof={state:'error',message:String(error)};}};
image.onerror=()=>{window.spriteProof={state:'error',message:'The local SVG did not decode.'};};
image.src='assets/seasonal-emoji.svg';
window.measureSeasonal=()=>{
  const overlay=document.querySelector('.seasonal-effects'),style=getComputedStyle(overlay),rect=overlay.getBoundingClientRect();
  return {visibility:document.visibilityState,hidden:document.hidden,sprite:window.spriteProof,
    overlay:{display:style.display,pointerEvents:style.pointerEvents,width:rect.width,height:rect.height},
    particles:[...document.querySelectorAll('.seasonal-particle')].map(p=>{const s=getComputedStyle(p),r=p.getBoundingClientRect(),a=p.getAnimations()[0],g=p.querySelector('.seasonal-glyph');return {
      time:a?.currentTime,state:a?.playState,transform:s.transform,opacity:Number(s.opacity),width:r.width,height:r.height,
      background:getComputedStyle(g).backgroundImage,glyphWidth:g.getBoundingClientRect().width
    };})};
};
)JS";
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5 || QByteArray(argv[1]) != "--ui-root" || QByteArray(argv[3]) != "--output")
            throw std::runtime_error("Use --ui-root SOURCE/UI --output PRIVATE-TEMP-DIRECTORY.");
        if (getuid() == 0 || prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1)
            throw std::runtime_error("Run the protected ordinary-user Python test runner.");
        for (const auto* name :
             {"QTWEBENGINE_DISABLE_SANDBOX", "QTWEBENGINE_CHROMIUM_FLAGS", "QTWEBENGINE_REMOTE_DEBUGGING"})
            if (qEnvironmentVariableIsSet(name) && !qgetenv(name).isEmpty())
                throw std::runtime_error("Browser sandbox overrides are not permitted in this regression.");
        const QString ui = QString::fromLocal8Bit(argv[2]), output = QString::fromLocal8Bit(argv[4]);
        if (!QDir().mkpath(output)) throw std::runtime_error("The test output directory cannot be created.");
        const auto productionCss = readAsset(ui + "/style.css");
        const auto productionSvg = readAsset(ui + "/assets/seasonal-emoji.svg");
        QWebEngineUrlScheme scheme("drifter");
        scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
                        QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled);
        QWebEngineUrlScheme::registerScheme(scheme);
        QApplication application(argc, argv);
        auto* profile = new QWebEngineProfile(&application);
        profile->setUrlRequestInterceptor(new Requests(profile));
        profile->installUrlSchemeHandler("drifter", new Assets({{"/ui/index.html", Html},
                                                                {"/ui/test.css", Css},
                                                                {"/ui/test.js", Script},
                                                                {"/ui/style.css", productionCss},
                                                                {"/ui/assets/seasonal-emoji.svg", productionSvg}},
                                                               profile));
        QStackedWidget stack;
        stack.resize(1200, 800);
        stack.addWidget(new QLabel("Loading seasonal rendering regression"));
        auto* view = new QWebEngineView;
        auto* page = new Page(profile, view);
        view->setPage(page);
        stack.addWidget(view);
        stack.show();
        QJsonObject frames;
        QJsonArray failures;
        int checks = 0;
        bool finished = false;
        const auto check = [&](bool condition, const QString& message) {
            ++checks;
            if (!condition) failures.append(message);
        };
        const auto finish = [&] {
            if (finished) return;
            finished = true;
            for (const auto& value : page->console)
                check(value.toObject().value("level").toInt() != int(QWebEnginePage::ErrorMessageLevel),
                      "A JavaScript error occurred.");
            QJsonObject report{{"passed", failures.isEmpty()},
                               {"checks", checks},
                               {"failures", failures},
                               {"qtVersion", qVersion()},
                               {"platform", QApplication::platformName()},
                               {"frames", frames},
                               {"console", page->console},
                               {"browserSandboxDisabled", false}};
            QSaveFile file(output + "/report.json");
            const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
            const bool saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
            std::printf("Native seasonal rendering: %d checks, %d failures.\n", checks, int(failures.size()));
            application.exit(saved && failures.isEmpty() ? 0 : 1);
        };
        const auto sample = [&](const QString& name, bool screenshot, bool final) {
            page->runJavaScript(
                "JSON.stringify(window.measureSeasonal ? window.measureSeasonal() : null)",
                [&, name, screenshot, final](const QVariant& value) {
                    auto data = QJsonDocument::fromJson(value.toString().toUtf8()).object();
                    check(!data.isEmpty(), "The native page did not return its rendering measurements.");
                    data["nativeViewVisible"] = view->isVisible();
                    data["nativePageVisible"] = page->isVisible();
                    data["nativeStackIndex"] = stack.currentIndex();
                    frames[name] = data;
                    if (screenshot)
                        check(view->grab().save(output + "/" + name + ".png"),
                              "A native screenshot could not be saved.");
                    if (!final) return;
                    const auto hidden = frames.value("hidden").toObject(),
                               first = frames.value("visible-start").toObject();
                    const auto last = frames.value("visible-end").toObject(),
                               reduced = frames.value("reduced-motion").toObject();
                    check(hidden.value("hidden").toBool() && !hidden.value("nativePageVisible").toBool(),
                          "The hidden startup view was not hidden.");
                    check(!last.value("hidden").toBool() && last.value("nativePageVisible").toBool() &&
                              last.value("nativeViewVisible").toBool(),
                          "The shown startup view did not become visible.");
                    check(last.value("nativeStackIndex").toInt() == 1, "The loading page remained selected.");
                    const auto sprite = last.value("sprite").toObject();
                    check(sprite.value("state") == "loaded" && sprite.value("width").toInt() == 768 &&
                              sprite.value("height").toInt() == 384,
                          "The local seasonal sprite did not decode at its expected size.");
                    const auto cells = sprite.value("cells").toArray();
                    check(cells.size() == 18, "The local sprite did not provide all eighteen glyphs.");
                    for (const auto& cell : cells) {
                        check(cell.toObject().value("pixels").toInt() > 20,
                              "A seasonal glyph painted no useful pixels.");
                        check(cell.toObject().value("colors").toInt() > 3, "A seasonal glyph did not paint in color.");
                    }
                    const auto before = first.value("particles").toArray(), after = last.value("particles").toArray();
                    check(before.size() == 18 && after.size() == 18,
                          "The production CSS did not animate eighteen particles.");
                    for (int i = 0; i < before.size() && i < after.size(); ++i) {
                        const auto a = before[i].toObject(), b = after[i].toObject();
                        check(b.value("state") == "running" &&
                                  b.value("time").toDouble() > a.value("time").toDouble() + 200,
                              "A native particle animation did not advance.");
                        check(b.value("transform") != a.value("transform"),
                              "A native particle transform remained frozen.");
                        check(
                            b.value("glyphWidth").toDouble() > 5 &&
                                b.value("background").toString().contains("drifter://app/ui/assets/seasonal-emoji.svg"),
                            "A particle did not use the local sprite background.");
                    }
                    check(last.value("overlay").toObject().value("pointerEvents") == "none",
                          "Seasonal artwork intercepted pointer input.");
                    check(reduced.value("overlay").toObject().value("display") == "none",
                          "Reduce motion did not hide the production overlay.");
                    finish();
                });
        };
        QObject::connect(page, &QWebEnginePage::loadFinished, &application, [&](bool ok) {
            if (!ok) {
                check(false, "The custom-scheme native page did not load.");
                finish();
                return;
            }
            QTimer::singleShot(500, &application, [&] { sample("hidden", false, false); });
            QTimer::singleShot(1000, &application, [&] { stack.setCurrentWidget(view); });
            QTimer::singleShot(2100, &application, [&] { sample("visible-start", true, false); });
            QTimer::singleShot(3400, &application, [&] { sample("visible-end", true, false); });
            QTimer::singleShot(3700, &application,
                               [&] { page->runJavaScript("document.documentElement.classList.add('reduce-motion')"); });
            QTimer::singleShot(4100, &application, [&] { sample("reduced-motion", false, true); });
        });
        QTimer::singleShot(20000, &application, [&] {
            check(false, "Native seasonal rendering timed out.");
            finish();
        });
        page->load(QUrl("drifter://app/ui/index.html"));
        return application.exec();
    } catch (const std::exception& issue) {
        std::fprintf(stderr, "Native seasonal rendering: %s\n", issue.what());
        return 1;
    }
}
