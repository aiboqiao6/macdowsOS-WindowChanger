#include "protocol.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QQuickView>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDebug>

// Ordinary application layouts. Styling is loaded later by the external host.
namespace {
void PaintCount(HWND window) {
    SetPropW(window, styler::SamplePaintProperty,
             reinterpret_cast<HANDLE>(reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty)) + 1));
}
class Browser : public QWidget {
  public:
    Browser() {
        setWindowTitle("Qt Widgets / original tab row");
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        resize(820, 500);
        setStyleSheet("QWidget { background: #f6f5f3; color: #30343b; font: 14px 'Segoe UI'; } QTabBar::tab { padding: "
                      "10px 28px; background: #e1e3e8; border: 0; } QTabBar::tab:selected { background: #ffffff; } "
                      "QLineEdit { background: white; padding: 10px; border: 1px solid #d9dce2; border-radius: 8px; } "
                      "QPushButton { border: 0; padding: 8px 12px; }");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *header = new QWidget;
        header->setFixedHeight(40);
        auto *row = new QHBoxLayout(header);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        auto *tabs = new QTabBar;
        tabs->addTab("Home");
        tabs->addTab("Research");
        tabs->addTab("Downloads");
        tabs->setExpanding(false);
        row->addWidget(tabs, 1);
        const char *names[]{"minimizeButton", "maximizeButton", "closeButton"};
        const QString labels[]{QStringLiteral("−"), QStringLiteral("□"), QStringLiteral("×")};
        for (int i = 0; i < 3; ++i) {
            auto *button = new QPushButton(labels[i]);
            button->setObjectName(names[i]);
            button->setFixedWidth(40);
            row->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, i] {
                if (i == 0)
                    showMinimized();
                else if (i == 1) {
                    if (isMaximized())
                        showNormal();
                    else
                        showMaximized();
                } else
                    close();
            });
        }
        layout->addWidget(header);
        auto *address = new QLineEdit("https://example.local/research");
        address->setObjectName("addressField");
        layout->addWidget(address);
        auto *body = new QLabel("Qt Widgets\n\nOriginal tabs and editable address field\n\nResize this window — the "
                                "layout belongs to this process.");
        body->setAlignment(Qt::AlignCenter);
        layout->addWidget(body, 1);
        connect(tabs, &QTabBar::currentChanged, body, [body](int index) {
            body->setText(QString("Tab %1\n\nOriginal application content remains interactive").arg(index + 1));
        });
    }

  protected:
    bool nativeEvent(const QByteArray &type, void *event, qintptr *result) override {
        if (reinterpret_cast<MSG *>(event)->message == WM_APP + 66) {
            for (auto *button : findChildren<QPushButton *>())
                button->show();
            *result = 1;
            return true;
        }
        if (reinterpret_cast<MSG *>(event)->message == WM_PAINT)
            PaintCount(reinterpret_cast<HWND>(winId()));
        return QWidget::nativeEvent(type, event, result);
    }
};
class QuickBrowser : public QQuickView {
  protected:
    bool nativeEvent(const QByteArray &type, void *event, qintptr *result) override {
        auto *message = reinterpret_cast<MSG *>(event);
        if (message->message == WM_APP + 66) {
            QMetaObject::invokeMethod(rootObject(), "refreshCaption");
            *result = 1;
            return true;
        }
        if (message->message == WM_APP + 64) {
            auto *tabs = rootObject()->findChild<QQuickItem *>("applicationTabs");
            *result = tabs ? qRound(tabs->property("leftPadding").toReal()) : -1;
            return true;
        }
        if (message->message == WM_APP + 65) {
            int visible{};
            for (auto *item : rootObject()->findChildren<QQuickItem *>())
                if (item->objectName().endsWith("Button") && item->isVisible())
                    ++visible;
            *result = visible;
            return true;
        }
        return QQuickView::nativeEvent(type, event, result);
    }
};
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Browser widgets;
    widgets.move(80, 100);
    widgets.show();
    SetPropW(reinterpret_cast<HWND>(widgets.winId()), styler::SampleProperty, reinterpret_cast<HANDLE>(1));
    QuickBrowser quick;
    quick.setTitle("Qt Quick / original GPU tab row");
    quick.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    quick.setResizeMode(QQuickView::SizeRootObjectToView);
    QQmlComponent component(quick.engine());
    component.loadUrl(QUrl("qrc:/qt-inline-target.qml"));
    QObject *root = component.create();
    if (!root) {
        qWarning() << component.errors();
        return 2;
    }
    quick.setContent(QUrl(), &component, root);
    quick.resize(820, 500);
    quick.setPosition(960, 100);
    quick.show();
    HWND window = reinterpret_cast<HWND>(quick.winId());
    SetPropW(window, styler::SampleProperty, reinterpret_cast<HANDLE>(1));
    QObject::connect(
        &quick, &QQuickWindow::afterRendering, &quick, [window] { PaintCount(window); }, Qt::DirectConnection);
    return app.exec();
}
