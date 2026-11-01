#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <chrono>
#include <deque>
#include <string>

using namespace geode::prelude;

struct HackDetectorState {
    bool noclipDetected = false;
    bool speedhackDetected = false;
    bool airJumpDetected = false;

    float realDeltaTimeRatio = 1.0f;
    int totalClicks = 0;
    int totalJumps = 0;
    
    std::deque<std::chrono::steady_clock::time_point> clickTimestamps;
    std::chrono::steady_clock::time_point lastFrameRealTime;
    
    void reset() {
        noclipDetected = false;
        speedhackDetected = false;
        airJumpDetected = false;
        realDeltaTimeRatio = 1.0f;
        totalClicks = 0;
        totalJumps = 0;
        clickTimestamps.clear();
        lastFrameRealTime = std::chrono::steady_clock::now();
    }

    float getCPS() {
        auto now = std::chrono::steady_clock::now();
        while (!clickTimestamps.empty() && 
               std::chrono::duration_cast<std::chrono::milliseconds>(now - clickTimestamps.front()).count() > 1000) {
            clickTimestamps.pop_front();
        }
        return static_cast<float>(clickTimestamps.size());
    }
};

static HackDetectorState g_state;

// Ventana flotante interactiva y arrastrable
class MovableFloatingHUD : public cocos2d::CCLayerColor {
public:
    cocos2d::CCPoint m_touchStartPos;
    cocos2d::CCPoint m_nodeStartPos;
    bool m_isDragging = false;

    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;

    static MovableFloatingHUD* create() {
        auto ret = new MovableFloatingHUD();
        // Caja de 220x90 con fondo semitransparente oscuro
        if (ret && ret->initWithColor(cocos2d::ccc4(15, 15, 20, 200), 220, 90)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() {
        this->setTouchEnabled(true);
        this->setTouchMode(cocos2d::kCCTouchesOneByOne);
        // Prioridad alta para capturar toques antes que PlayLayer
        this->setTouchPriority(-500);

        // Barra superior
        m_titleLabel = cocos2d::CCLabelBMFont::create("::: DETECTOR DE HACKS :::", "chatFont.fnt");
        m_titleLabel->setAnchorPoint({0.0f, 1.0f});
        m_titleLabel->setPosition({10.0f, 85.0f});
        m_titleLabel->setScale(0.40f);
        m_titleLabel->setColor(cocos2d::ccc3(180, 180, 255));
        this->addChild(m_titleLabel);

        // Estado de Hacks
        m_statusLabel = cocos2d::CCLabelBMFont::create("Hacks: Ninguno (Limpio)", "chatFont.fnt");
        m_statusLabel->setAnchorPoint({0.0f, 1.0f});
        m_statusLabel->setPosition({10.0f, 65.0f});
        m_statusLabel->setScale(0.55f);
        m_statusLabel->setColor(cocos2d::ccc3(80, 255, 120));
        this->addChild(m_statusLabel);

        // Estadísticas
        m_statsLabel = cocos2d::CCLabelBMFont::create("CPS: 0.0 | Clics: 0\nSaltos: 0 | Speed: 1.00x", "chatFont.fnt");
        m_statsLabel->setAnchorPoint({0.0f, 1.0f});
        m_statsLabel->setPosition({10.0f, 42.0f});
        m_statsLabel->setScale(0.48f);
        m_statsLabel->setColor(cocos2d::ccc3(220, 220, 220));
        this->addChild(m_statsLabel);

        return true;
    }

    // --- Control de gestos para mover la ventana ---
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        cocos2d::CCPoint localPoint = this->convertTouchToNodeSpace(touch);
        cocos2d::CCRect bounds = cocos2d::CCRect(0, 0, this->getContentSize().width, this->getContentSize().height);

        if (bounds.containsPoint(localPoint)) {
            m_isDragging = true;
            m_touchStartPos = touch->getLocation();
            m_nodeStartPos = this->getPosition();
            return true; // Reclama el toque para que el cubo no salte al mover el HUD
        }
        return false;
    }

    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        if (m_isDragging) {
            cocos2d::CCPoint currentTouch = touch->getLocation();
            cocos2d::CCPoint delta = ccpSub(currentTouch, m_touchStartPos);
            this->setPosition(ccpAdd(m_nodeStartPos, delta));
        }
    }

    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        m_isDragging = false;
    }

    void updateDisplay() {
        std::string hacks = "Hacks: ";
        bool any = false;

        if (g_state.noclipDetected) {
            hacks += "[Noclip] ";
            any = true;
        }
        if (g_state.speedhackDetected) {
            hacks += "[Speedhack] ";
            any = true;
        }
        if (g_state.airJumpDetected) {
            hacks += "[AirJump] ";
            any = true;
        }

        if (!any) {
            m_statusLabel->setString("Hacks: Ninguno (Limpio)");
            m_statusLabel->setColor(cocos2d::ccc3(80, 255, 120));
        } else {
            m_statusLabel->setString(hacks.c_str());
            m_statusLabel->setColor(cocos2d::ccc3(255, 70, 70));
        }

        std::string stats = fmt::format(
            "CPS: {:.1f} | Clics: {}\nSaltos: {} | Speed: {:.2f}x",
            g_state.getCPS(),
            g_state.totalClicks,
            g_state.totalJumps,
            g_state.realDeltaTimeRatio
        );
        m_statsLabel->setString(stats.c_str());
    }
};

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        MovableFloatingHUD* m_hud = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        g_state.reset();

        auto winSize = cocos2d::CCDirector::get()->getWinSize();
        m_fields->m_hud = MovableFloatingHUD::create();
        m_fields->m_hud->setPosition({20.0f, winSize.height - 110.0f});
        m_fields->m_hud->setZOrder(1000);
        this->addChild(m_fields->m_hud);

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - g_state.lastFrameRealTime;
        g_state.lastFrameRealTime = now;

        float realDt = elapsed.count();
        if (realDt > 0.0001f && dt > 0.0001f) {
            float ratio = dt / realDt;
            g_state.realDeltaTimeRatio = ratio;
            if (ratio > 1.15f || ratio < 0.85f) {
                g_state.speedhackDetected = true;
            }
        }

        if (m_fields->m_hud) {
            m_fields->m_hud->updateDisplay();
        }
    }
};

class $modify(MyPlayerObject, PlayerObject) {
    void pushButton(PlayerButton p0) {
        PlayerObject::pushButton(p0);
        if (p0 == PlayerButton::Jump) {
            g_state.totalClicks++;
            g_state.clickTimestamps.push_back(std::chrono::steady_clock::now());
        }
    }

    void incrementJumps() {
        PlayerObject::incrementJumps();
        g_state.totalJumps++;
    }
};
