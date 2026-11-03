#include "protocol.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QQuickView>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>
#include <array>

namespace {
void CountPaint(HWND window) {
    auto count = reinterpret_cast<UINT_PTR>(GetPropW(window, styler::SamplePaintProperty));
    SetPropW(window, styler::SamplePaintProperty, reinterpret_cast<HANDLE>(count + 1));
}
class SampleWidget : public QWidget {
  protected:
    bool nativeEvent(const QByteArray &type, void *event, qintptr *result) override {
        // Qt can satisfy WM_PAINT by blitting its existing backing store,
        // without generating a new QWidget::paintEvent when size is unchanged.
        if (reinterpret_cast<MSG *>(event)->message == WM_PAINT)
            CountPaint(reinterpret_cast<HWND>(winId()));
        return QWidget::nativeEvent(type, event, result);
    }
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        CountPaint(reinterpret_cast<HWND>(winId()));
    }
};
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    std::array<SampleWidget, 2> widgets;
    for (int i = 0; i < 2; ++i) {
        auto &window = widgets[i];
        window.setWindowTitle(i ? "Actual Qt Widgets - Frameless" : "Actual Qt Widgets - Native");
        if (i)
            window.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint |
                                  Qt::WindowCloseButtonHint);
        window.resize(600, 360);
        auto *layout = new QVBoxLayout(&window);
        layout->addWidget(new QLabel("Real Qt Widgets content. The original toolbar stays in the client area."));
        layout->addWidget(new QLineEdit("Qt line edit survives attach and restore"));
        window.show();
        SetPropW(reinterpret_cast<HWND>(window.winId()), styler::SampleProperty, reinterpret_cast<HANDLE>(1));
    }
    QQuickView quick;
    quick.setTitle("Actual Qt Quick - GPU Frameless");
    quick.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    quick.setResizeMode(QQuickView::SizeRootObjectToView);
    QQmlComponent component(quick.engine());
    component.setData(R"(
        import QtQuick
        Rectangle {
            width: 600; height: 360
            gradient: Gradient {
                GradientStop { position: 0; color: "#e5eaf5" }
                GradientStop { position: 1; color: "#d8e8dc" }
            }
            Text { anchors.centerIn: parent; text: "Real Qt Quick / GPU content"; font.pixelSize: 22; color: "#29394c" }
            Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 18; height: 18; color: "#3f73d0" }
        }
    )",
                      QUrl());
    QObject *root = component.create();
    if (!root)
        return 2;
    quick.setContent(QUrl(), &component, root);
    quick.resize(600, 360);
    HWND quickHwnd = reinterpret_cast<HWND>(quick.winId());
    QObject::connect(
        &quick, &QQuickWindow::afterRendering, &quick, [quickHwnd] { CountPaint(quickHwnd); }, Qt::DirectConnection);
    quick.show();
    SetPropW(quickHwnd, styler::SampleProperty, reinterpret_cast<HANDLE>(1));
    // Recheck exposure-driven rendering after the injected frame transition.
    QTimer::singleShot(50, &quick, [&quick] { quick.update(); });
    return application.exec();
}
