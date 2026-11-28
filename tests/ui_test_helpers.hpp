#pragma once

// Finding things in a running QML scene and driving them with the mouse, the
// way an operator does. For the UI tests.

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QTest>
#include <QtQuickTest/quicktest.h>

namespace seventhslide::test {

/// A QML-defined object (not only an item) under ``parent``, by its type name.
inline QObject* qml_object(QObject* parent, const char* type)
{
    const QByteArray prefix = QByteArray(type) + "_QMLTYPE_";
    for (QObject* o : parent->findChildren<QObject*>()) {
        if (QByteArray(o->metaObject()->className()).startsWith(prefix)) {
            return o;
        }
    }
    return nullptr;
}

/// The first item on screen under ``root`` showing ``text``, found the way a
/// user finds a row. Walks the visual tree: a view's delegates are not
/// QObject children of the view.
inline QQuickItem* visible_text_under(QQuickItem* root, const QString& text)
{
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (item->property("text").toString() == text && item->width() > 0) {
            return item;
        }
        if (QQuickItem* found = visible_text_under(item, text)) {
            return found;
        }
    }
    return nullptr;
}

/// Every visible item under ``root`` whose class inherits ``type``, walking
/// the visual tree (delegates are visual children, not QObject children).
inline QList<QQuickItem*> visible_items_of(QQuickItem* root, const char* type)
{
    QList<QQuickItem*> out;
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (item->inherits(type)) {
            out.push_back(item);
        }
        out += visible_items_of(item, type);
    }
    return out;
}

/// Every visible item under ``root`` whose QML type is ``type`` (a type
/// declared in QML, which ``inherits`` cannot name), walking the visual tree.
inline QList<QQuickItem*> visible_qml_items_of(QQuickItem* root, const char* type)
{
    QList<QQuickItem*> out;
    const QByteArray prefix = QByteArray(type) + "_QMLTYPE_";
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (QByteArray(item->metaObject()->className()).startsWith(prefix)) {
            out.push_back(item);
        }
        out += visible_qml_items_of(item, type);
    }
    return out;
}

/// Whether ``object``'s QML type is ``type`` or one built on it (an
/// IconToolButton is an AppButton).
inline bool is_qml_type(const QObject* object, const char* type)
{
    const QByteArray prefix = QByteArray(type) + "_QMLTYPE_";
    for (const QMetaObject* m = object->metaObject(); m != nullptr; m = m->superClass()) {
        if (QByteArray(m->className()).startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

/// The Controller's well of what is live (its Program's), under ``root``
/// (the window): what a test of the live page, video or source looks in, as
/// a stack with Preview on has a second well beside it.
inline QQuickItem* program_well(QObject* root)
{
    return root->findChild<QQuickItem*>(QStringLiteral("programWell"));
}

/// The visible button (an AppButton, or a type built on it) under ``root``
/// whose objectName is ``name``; null when there is none.
inline QQuickItem* visible_button_under(QQuickItem* root, const QString& name)
{
    for (QQuickItem* item : visible_items_of(root, "QQuickItem")) {
        if (is_qml_type(item, "AppButton") && item->objectName() == name) {
            return item;
        }
    }
    return nullptr;
}

/// The visible view (ListView, GridView or Repeater) under ``root`` that
/// shows ``model``.
inline QQuickItem* list_view_of(QObject* root, const QObject* model)
{
    for (QQuickItem* item : root->findChildren<QQuickItem*>()) {
        const bool view = item->inherits("QQuickItemView") || item->inherits("QQuickRepeater");
        if (view && item->isVisible() && item->property("model").value<QObject*>() == model) {
            return item;
        }
    }
    return nullptr;
}

/// Row ``index`` of ``view`` (from list_view_of), which must be laid out.
inline QQuickItem* row_of(QQuickItem* view, int index)
{
    QQuickItem* item = nullptr;
    const char* method = view->inherits("QQuickRepeater") ? "itemAt" : "itemAtIndex";
    REQUIRE(QMetaObject::invokeMethod(view, method, Q_RETURN_ARG(QQuickItem*, item),
                                      Q_ARG(int, index)));
    INFO("row " << index);
    REQUIRE(item != nullptr);
    return item;
}

/// Wait until ``window`` has laid out what changed. A Column, a Row or a
/// Layout places its items on the window's next frame, not as they change:
/// until then an item just shown stands where it last was, perhaps over
/// another, and a click sent there reaches the wrong one. On a busy
/// computer that frame can come well after a short wait.
inline void laid_out(const QQuickWindow* window)
{
    if (window != nullptr) {
        (void)QQuickTest::qWaitForPolish(window, 2000);
    }
}

/// The middle of ``item`` in its window's scene, once the window is laid out.
inline QPoint centre_of(QQuickItem* item)
{
    laid_out(item->window());
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

/// Click at ``at`` with ``modifiers`` held, once the window is laid out.
inline void click(QQuickWindow* window, QPoint at, Qt::KeyboardModifiers modifiers = {})
{
    laid_out(window);
    QTest::mouseClick(window, Qt::LeftButton, modifiers, at);
    QTest::qWait(10);
}

/// Press at ``from``, move to ``to`` in steps as a hand would, release.
inline void drag(QQuickWindow* window, QPoint from, QPoint to,
                 Qt::KeyboardModifiers modifiers = {})
{
    laid_out(window);
    QTest::mousePress(window, Qt::LeftButton, modifiers, from);
    constexpr int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        QTest::mouseMove(window, from + (to - from) * i / steps);
        QTest::qWait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, modifiers, to);
    QTest::qWait(20);
}

}  // namespace seventhslide::test
