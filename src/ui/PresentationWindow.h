// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation window (document camera / visualiser mode): full-speed live view
// with zoom, rotation and mirroring, freeze, drawing over the picture, screenshot
// and video recording with sound.
#pragma once

#include "app/AppSettings.h"
#include "device/CameraThread.h"
#include "media/Recorder.h"

#include <QColor>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QComboBox;
class QListWidget;
class QLabel;
class QToolButton;

namespace lensscan {

enum class DrawTool { Move, Pen, Highlighter, Line, Arrow, Rect, Ellipse, Eraser, Triangle, Text };

struct Annotation {
    DrawTool tool = DrawTool::Pen;
    QColor color;
    double width = 4; // content pixels
    QVector<QPointF> points; // content coordinates; two points for shapes, one for text
    Qt::PenStyle style = Qt::SolidLine;
    QString text; // Text tool
};

// Shows the (rotated / mirrored) camera picture, zoomed on a part of it, with the
// annotations on top. Annotations are stored in picture coordinates, so they stay
// on the document when zooming or panning.
class PresentationCanvas : public QWidget {
    Q_OBJECT
public:
    explicit PresentationCanvas(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);   // raw camera frame
    void setFrozen(bool on) { m_frozen = on; }
    bool isFrozen() const { return m_frozen; }
    void setTool(DrawTool t);
    void setColor(const QColor &c) { m_color = c; }
    void setPenWidth(int screenPx) { m_penWidth = screenPx; }
    void setPenStyle(Qt::PenStyle s) { m_penStyle = s; }
    void rotate(int degrees);             // +-90
    void flip(bool horizontal);
    void resetZoom();
    double zoom() const { return m_zoom; }
    void undo();
    void clearAnnotations();
    bool hasAnnotations() const { return !m_items.isEmpty(); }
    void setMessage(const QString &text);
    void flash();
    // What the window shows (visible part, annotations included) at picture resolution.
    QImage renderView() const;
    QSize contentSize() const { return m_content.size(); }

Q_SIGNALS:
    void framePainted();
    void zoomChanged(double zoom);
    void changed(); // picture, view or annotations changed

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    QImage transformed(const QImage &raw) const;
    QRectF visibleRect() const;  // in content coordinates
    QRectF targetRect() const;   // in widget coordinates
    QPointF toContent(const QPointF &widgetPos) const;
    double viewScale() const;    // widget px per content px
    void clampCenter();
    void drawAnnotations(QPainter &p, const QVector<Annotation> &items) const;
    void eraseAt(const QPointF &contentPos);
    void pushUndo();
    void mapAnnotations(const std::function<QPointF(const QPointF &)> &f);

    QImage m_content;
    int m_rotation = 0;
    bool m_flipH = false, m_flipV = false;
    bool m_frozen = false;
    double m_zoom = 1.0;
    QPointF m_center; // content coordinates of the view centre
    DrawTool m_tool = DrawTool::Move;
    QColor m_color = QColor(0xe5, 0x39, 0x35);
    int m_penWidth = 4;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
    QVector<Annotation> m_items;
    QVector<QVector<Annotation>> m_undo;
    bool m_drawing = false, m_panning = false, m_erased = false;
    Annotation m_current;
    QPointF m_panStart, m_panCenter;
    QString m_message;
    int m_flash = 0;
};

class PresentationWindow : public QWidget {
    Q_OBJECT
public:
    explicit PresentationWindow(AppSettings &settings, QWidget *parent = nullptr);
    ~PresentationWindow() override;

Q_SIGNALS:
    void closed();
    void fileSaved(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildTopBar();
    QWidget *buildToolBar();
    void onFrame(const QImage &frame);
    void applyControls(const QVector<ControlInfo> &controls);
    void setTool(DrawTool t);
    void chooseColor(const QColor &c);
    void toggleFreeze();
    void screenshot();
    void toggleRecording();
    void togglePause();
    void toggleScreenRecording();
    void startScreenRecording(int pipewireFd, quint32 node);
    void refreshMedia();
    void updateDeviceLabel(const QSize &size);
    void finishRecording(bool ok, const QString &error);
    void toggleFullScreen();
    void back();
    void updateRecordUi();
    QString outputPath(const QString &dir, const QString &kind, const QString &suffix) const;
    void stopRecording();

    AppSettings &m_settings;
    CameraThread *m_camera;
    PresentationCanvas *m_canvas;
    Recorder m_recorder;
    QTimer m_recordTimer;
    bool m_dirty = false;
    QString m_videoPath;
    QFutureWatcher<QPair<bool, QString>> m_stopWatcher;
    QFutureWatcher<QList<AudioSource>> m_sourcesWatcher;
    QButtonGroup *m_tools = nullptr;
    QToolButton *m_colorButton = nullptr, *m_freezeButton = nullptr, *m_recordButton = nullptr;
    QToolButton *m_rotL = nullptr, *m_rotR = nullptr;
    QToolButton *m_pauseButton = nullptr, *m_screenButton = nullptr, *m_mediaButton = nullptr;
    QListWidget *m_media = nullptr;
    class ScreenCastPortal *m_portal = nullptr;
    QComboBox *m_audio = nullptr;
    QComboBox *m_videoSize = nullptr;
    QString m_cameraName;
    QLabel *m_deviceLabel = nullptr, *m_zoomLabel = nullptr, *m_recLabel = nullptr;
};

} // namespace lensscan
