"""Render the app icon (a cylinder with a lightning bolt) to resources/icon.png.

Run from rpu_console/:  python tools/make_icon.py
"""
import sys
from pathlib import Path

from PyQt6.QtCore import QPointF, QRectF, Qt
from PyQt6.QtGui import (QBrush, QColor, QGuiApplication, QImage, QLinearGradient, QPainter,
                         QPainterPath, QPen, QPolygonF, QRadialGradient)

SIZE = 1024
OUT = Path(__file__).resolve().parent.parent / "rpu_console" / "resources" / "icon.png"


def cylinder_path(left, right, top, bottom, ry):
    """Silhouette of a cylinder: flat-bottom ellipse arcs, straight sides."""
    w = right - left
    p = QPainterPath()
    p.moveTo(left, top)
    p.arcTo(QRectF(left, top - ry, w, 2 * ry), 180, -180)       # top rim, front half... closed below
    p.lineTo(right, bottom)
    p.arcTo(QRectF(left, bottom - ry, w, 2 * ry), 0, -180)       # bottom curve
    p.closeSubpath()
    return p


def main():
    app = QGuiApplication(sys.argv)  # noqa: F841 (needed for QImage/QPainter font+gui init)
    img = QImage(SIZE, SIZE, QImage.Format.Format_ARGB32)
    img.fill(Qt.GlobalColor.transparent)
    p = QPainter(img)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)

    # Rounded-square tile with a deep blue gradient (macOS-style margin around it).
    m = 64
    tile = QRectF(m, m, SIZE - 2 * m, SIZE - 2 * m)
    bg = QLinearGradient(0, tile.top(), 0, tile.bottom())
    bg.setColorAt(0, QColor("#22324f"))
    bg.setColorAt(1, QColor("#0b1220"))
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(bg))
    p.drawRoundedRect(tile, 190, 190)

    # Cylinder body.
    left, right, top, bottom, ry = 262, 762, 330, 770, 105
    body = cylinder_path(left, right, top, bottom, ry)
    metal = QLinearGradient(left, 0, right, 0)
    metal.setColorAt(0.00, QColor("#5b6b86"))
    metal.setColorAt(0.28, QColor("#c3cfe2"))
    metal.setColorAt(0.55, QColor("#8796b0"))
    metal.setColorAt(1.00, QColor("#3a4760"))
    p.setBrush(QBrush(metal))
    p.drawPath(body)

    # Top face (full ellipse) lit from above.
    cap = QRadialGradient(QPointF(450, top - 20), 330)
    cap.setColorAt(0, QColor("#f2f6fc"))
    cap.setColorAt(1, QColor("#9fb0cb"))
    p.setBrush(QBrush(cap))
    p.drawEllipse(QRectF(left, top - ry, right - left, 2 * ry))

    # Rim line and a lower band to suggest segments.
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.setPen(QPen(QColor(255, 255, 255, 70), 5))
    p.drawEllipse(QRectF(left + 22, top - ry + 18, right - left - 44, 2 * ry - 36))
    p.setPen(QPen(QColor(10, 18, 32, 90), 6))
    band = QPainterPath()
    band.moveTo(left, 560)
    band.arcTo(QRectF(left, 560 - ry, right - left, 2 * ry), 180, 180)
    p.drawPath(band)

    # String with a wiggle, rising from the top face (a tether).
    sx = (left + right) / 2  # centre of the top face
    string = QPainterPath()
    string.moveTo(sx, top)
    string.cubicTo(sx - 70, top - 50, sx + 80, top - 90, sx + 10, top - 130)
    string.cubicTo(sx - 60, top - 170, sx + 70, top - 185, sx + 15, top - 205)
    for width, color in ((24, QColor(11, 18, 32, 220)), (14, QColor("#f1f5fb"))):
        p.setPen(QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap,
                      Qt.PenJoinStyle.RoundJoin))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawPath(string)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor("#e8eef8"))
    p.drawEllipse(QPointF(sx, top), 20, 11)  # where it enters the cylinder

    # Lightning bolt, overlapping the cylinder front and top face.
    bolt = QPolygonF([
        QPointF(590, 170), QPointF(405, 545), QPointF(520, 545),
        QPointF(440, 870), QPointF(660, 470), QPointF(540, 470), QPointF(640, 170),
    ])
    bolt.translate(60, 0)  # leave room for the string at the centre of the top face
    glow = QPainterPath()
    glow.addPolygon(bolt)
    glow.closeSubpath()
    for width, alpha in ((46, 40), (30, 70), (16, 110)):
        p.setPen(QPen(QColor(255, 196, 40, alpha), width, Qt.PenStyle.SolidLine,
                      Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawPath(glow)
    fill = QLinearGradient(0, 170, 0, 870)
    fill.setColorAt(0, QColor("#fff3a8"))
    fill.setColorAt(0.5, QColor("#ffc928"))
    fill.setColorAt(1, QColor("#ff8a00"))
    p.setPen(QPen(QColor("#0b1220"), 14, Qt.PenStyle.SolidLine,
                  Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
    p.setBrush(QBrush(fill))
    p.drawPath(glow)

    p.end()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(str(OUT))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
