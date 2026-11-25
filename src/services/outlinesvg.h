#pragma once

#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QSvgGenerator>

// Use QPaintEngine's default text implementation, which sends the shaped
// glyph outlines to drawPath(). SVG therefore keeps the measured fonts and
// positions even when a different renderer displays it.
class OutlineSvg : public QPaintDevice
{
    class Engine : public QPaintEngine
    {
    public:
        explicit Engine(QSvgGenerator &svg)
            : QPaintEngine(svgFeatures()), m_svg(svg)
        {
        }

        bool begin(QPaintDevice *) override { return m_painter.begin(&m_svg); }
        bool end() override { return m_painter.end(); }
        Type type() const override { return User; }

        void updateState(const QPaintEngineState &state) override
        {
            const auto dirty = state.state();
            if (dirty.testFlag(DirtyPen))
                m_painter.setPen(state.pen());
            if (dirty.testFlag(DirtyBrush))
                m_painter.setBrush(state.brush());
            if (dirty.testFlag(DirtyTransform))
                m_painter.setWorldTransform(state.transform());
            if (dirty.testFlag(DirtyOpacity))
                m_painter.setOpacity(state.opacity());
        }

        void drawPath(const QPainterPath &path) override { m_painter.drawPath(path); }
        void drawPolygon(const QPointF *points, int count, PolygonDrawMode mode) override
        {
            if (mode == PolylineMode)
                m_painter.drawPolyline(points, count);
            else
                m_painter.drawPolygon(points, count, mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
        }

        void drawPixmap(const QRectF &to, const QPixmap &pixmap, const QRectF &from) override
        {
            m_painter.drawPixmap(to, pixmap, from);
        }

    private:
        static PaintEngineFeatures svgFeatures()
        {
            return PainterPaths | PrimitiveTransform | AlphaBlend | Antialiasing;
        }

        QSvgGenerator &m_svg;
        QPainter m_painter;
    };

public:
    OutlineSvg(QIODevice *output, const QSize &size)
        : m_engine(m_svg)
    {
        m_svg.setOutputDevice(output);
        m_svg.setSize(size);
        m_svg.setViewBox(QRect(QPoint(), size));
        m_svg.setResolution(96);
    }

    QPaintEngine *paintEngine() const override { return &m_engine; }

protected:
    int metric(PaintDeviceMetric value) const override
    {
        switch (value) {
        case PdmWidth: return m_svg.width();
        case PdmHeight: return m_svg.height();
        case PdmWidthMM: return m_svg.widthMM();
        case PdmHeightMM: return m_svg.heightMM();
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY: return m_svg.resolution();
        case PdmDepth: return 32;
        default: return QPaintDevice::metric(value);
        }
    }

private:
    QSvgGenerator m_svg;
    mutable Engine m_engine;
};
