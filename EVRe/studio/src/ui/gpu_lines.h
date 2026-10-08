/* SPDX-License-Identifier: Apache-2.0 */
/* GpuLines: the chart's plot drawn by a graphics card, straight to the screen.
 *
 * The chart is a raster widget, and stays one: an OpenGL widget in the window
 * made Qt compose the whole window on the GPU (chart_widget.h). The plot is a
 * layer of the window instead (DirectComposition): the system shows it over the
 * window's own pixels where the plot is, from the frames the card draws and
 * presents into it (a swap chain): the background, the grid lines, the lines,
 * and what goes over them (the cursors, the crosshair and its box). Nothing
 * comes back to the processor, and Qt neither copies the plot nor sends its
 * pixels to the screen: at 4K that was half of a frame. The window's thread
 * only lists what to draw.
 *
 * A layer, not a native child window: Windows took a child window away at once
 * when its tab was hidden, before Qt had painted the new tab (the window's old
 * pixels showed through for a tenth of a second), and showed its old frame when
 * it came back. A layer is shown and taken away when the chart says
 * (setShown), at the screen's next refresh, together with what the window
 * painted just before.
 *
 * A line is a row of segments; each segment is one instance, a quad as wide as
 * the line made in the vertex shader, 4x antialiased. What goes over the plot
 * are pictures (the crosshair's box, its dots, the cursors' tags) copied at
 * whole pixels, each kept on the card while it is the same picture. The GPU is
 * chosen by name among the adapters the system lists: a dedicated card (its own
 * memory) or the one in the processor.
 *
 * Windows: Direct3D 11 (shaders compiled at run time). Elsewhere: no adapters,
 * the chart draws on the CPU. */
#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRgb>
#include <QString>
#include <QVector>
#include <qwindowdefs.h> /* WId */
#include <memory>

class GpuLines {
public:
	struct Adapter {
		QString name;
		quint32 luidLow = 0; /* the system's id of the adapter */
		qint32 luidHigh = 0;
		bool dedicated = false; /* a card with memory of its own; false: the processor's */
	};
	/* a segment in the window's pixels; rgba: the colour as bytes R, G, B, A in memory */
	struct Segment {
		float x0, y0, x1, y1;
		quint32 rgba;
	};
	/* segments of one width. caps: each end goes on by half the width, so a line's segments meet at its points;
	 * without: a bar exactly as long as the segment (a span shaded across the plot) */
	struct Layer {
		QVector<Segment> segments;
		float widthPx = 1;
		bool caps = true;
	};
	/* a picture (premultiplied alpha) copied with its top left at a whole pixel of the window */
	struct Sprite {
		QImage image;
		QPoint at;
	};
	/* one frame: the background, the layers in turn, then the pictures over them */
	struct Frame {
		QRgb background = 0;
		QVector<Layer> layers;
		QVector<Sprite> sprites;
	};

	GpuLines();
	~GpuLines();

	/* the adapters that can draw, dedicated ones first (none on a system without Direct3D 11) */
	static QVector<Adapter> adapters();
	bool open(const Adapter &adapter, QString &error);
	QString name() const; /* the adapter drawing */

	/* Draws the frame into the layer of the top-level window `window`, at `pixels` of its client area (the frame's
	 * size), and presents it: on the screen at once while the layer is shown. False: the card failed (it is closed
	 * then, its layer gone, and `error` says why). */
	bool present(WId window, const QRect &pixels, const Frame &frame, QString &error);
	/* the frames present() let go because the system was still busy with the one before: the layer kept its last one
	 * (a held chart paints again, ChartView::plotOnGpu); the size of the last frame that reached the layer; tests: the
	 * next frame let go as if the system were busy */
	int droppedFrames() const;
	QSize presentedSize() const;
	void dropNextFrame();
	/* the layer over the window, or not (the window's own pixels there): at the screen's next refresh. False: the card
	 * failed (closed, as present) */
	bool setShown(bool shown, QString &error);
	bool shown() const;
	/* the last frame drawn, read back from the card, waited for (null: none yet): the chart paints it under the layer
	 * before the layer is shown; tests */
	QImage lastPicture();

	struct Impl; /* the device and what it draws with (gpu_lines.cpp) */

private:
	std::unique_ptr<Impl> d_;
};
