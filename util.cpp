/*
    Copyright 2011-2012 Heikki Holstila <heikki.holstila@gmail.com>

    This file is part of FingerTerm.

    FingerTerm is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    FingerTerm is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FingerTerm.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "qplatformdefs.h"

#include <QtCore>
#include <QtGui>
#include <QGuiApplication>
#include <QQuickView>
#include <QDebug>

#include "mainwindow.h"
#include "terminal.h"
#include "util.h"
#include "textrender.h"
#include "version.h"

#ifdef HAVE_FEEDBACK
#include <QFeedbackEffect>
#endif

Util::Util(QSettings *settings, QObject *parent) :
    QObject(parent),
    iAllowGestures(false),
    newSelection(true),
    iSettings(settings),
    iWindow(0),
    iRenderer(0)
{
    connect(QGuiApplication::clipboard(), SIGNAL(dataChanged()), this, SIGNAL(clipboardOrSelectionChanged()));
}

Util::~Util()
{
}

void Util::setWindow(QQuickView* win)
{
    iWindow = dynamic_cast<MainWindow*>(win);
    if(!iWindow)
        qFatal("invalid main window");
}

void Util::setWindowTitle(QString title)
{
    iCurrentWinTitle = title;
    iWindow->setTitle(title);
    emit windowTitleChanged();
}

QString Util::currentWindowTitle()
{
    return iCurrentWinTitle;
}

void Util::windowMinimize()
{
    iWindow->minimize();
}

void Util::openNewWindow()
{
    QProcess::startDetached("/usr/bin/fingerterm");
}

QString Util::configPath()
{
    if(!iSettings)
        return QString();

    QFileInfo f(iSettings->fileName());
    return f.path();
}

QVariant Util::settingsValue(QString key)
{
    if(!iSettings)
        return QVariant();

    return iSettings->value(key);
}

void Util::setSettingsValue(QString key, QVariant value)
{
    if(iSettings)
        iSettings->setValue(key, value);
}

QString Util::versionString()
{
    return PROGRAM_VERSION;
}

int Util::uiFontSize()
{
    return 12;
}

void Util::keyPressFeedback()
{
    if( !settingsValue("ui/keyPressFeedback").toBool() )
        return;

#ifdef HAVE_FEEDBACK
    QFeedbackEffect::playThemeEffect(QFeedbackEffect::PressWeak);
#endif
}

void Util::keyReleaseFeedback()
{
    if( !settingsValue("ui/keyPressFeedback").toBool() )
        return;

    // TODO: check what's more comfortable, only press, or press and release
#ifdef HAVE_FEEDBACK
    QFeedbackEffect::playThemeEffect(QFeedbackEffect::ReleaseWeak);
#endif
}

void Util::bellAlert()
{
    if(!iWindow)
        return;

    if( settingsValue("general/visualBell").toBool() ) {
        emit visualBell();
    }
}

void Util::mousePress(float eventX, float eventY) {
    if(!iAllowGestures)
        return;

    dragOrigin = QPointF(eventX, eventY);
    newSelection = true;
}

void Util::mouseMove(float eventX, float eventY) {
    QPointF eventPos(eventX, eventY);

    if(!iAllowGestures)
        return;

    if(settingsValue("ui/dragMode")=="scroll") {
        dragOrigin = scrollBackBuffer(eventPos, dragOrigin);
    }
    else if(settingsValue("ui/dragMode")=="select" && iRenderer) {
        selectionHelper(eventPos);
    }
}

void Util::mouseRelease(float eventX, float eventY) {
    QPointF eventPos(eventX, eventY);
    const int reqDragLength = 140;

    if(!iAllowGestures)
        return;

    if(settingsValue("ui/dragMode")=="gestures") {
        int xdist = qAbs(eventPos.x() - dragOrigin.x());
        int ydist = qAbs(eventPos.y() - dragOrigin.y());
        if(eventPos.x() < dragOrigin.x()-reqDragLength && xdist > ydist*2)
            doGesture(PanLeft);
        else if(eventPos.x() > dragOrigin.x()+reqDragLength && xdist > ydist*2)
            doGesture(PanRight);
        else if(eventPos.y() > dragOrigin.y()+reqDragLength && ydist > xdist*2)
            doGesture(PanDown);
        else if(eventPos.y() < dragOrigin.y()-reqDragLength && ydist > xdist*2)
            doGesture(PanUp);
    }
    else if(settingsValue("ui/dragMode")=="scroll") {
        scrollBackBuffer(eventPos, dragOrigin);
    }
    else if(settingsValue("ui/dragMode")=="select" && iRenderer) {
        selectionHelper(eventPos);
        selectionFinished();
    }
}

void Util::selectionHelper(QPointF scenePos)
{
    int yCorr =  iRenderer->fontDescent();

    QPoint start(qRound((dragOrigin.x()+2)/iRenderer->fontWidth()),
                 qRound((dragOrigin.y()+yCorr)/iRenderer->fontHeight()));
    QPoint end(qRound((scenePos.x()+2)/iRenderer->fontWidth()),
               qRound((scenePos.y()+yCorr)/iRenderer->fontHeight()));

    if (start != end) {
        iTerm->setSelection(start, end);
        newSelection = false;
    }
}

QPointF Util::scrollBackBuffer(QPointF now, QPointF last)
{
    if(!iTerm)
        return last;

    int xdist = qAbs(now.x() - last.x());
    int ydist = qAbs(now.y() - last.y());
    int fontSize = settingsValue("ui/fontSize").toInt();

    int lines = ydist / fontSize;

    if(lines > 0 && now.y() < last.y() && xdist < ydist*2) {
        iTerm->scrollBackBufferFwd(lines);
        last = QPointF(now.x(), last.y() - lines * fontSize);
    } else if(lines > 0 && now.y() > last.y() && xdist < ydist*2) {
        iTerm->scrollBackBufferBack(lines);
        last = QPointF(now.x(), last.y() + lines * fontSize);
    }

    return last;
}

void Util::doGesture(Util::PanGesture gesture)
{
    if(!iTerm)
        return;

    if( gesture==PanLeft ) {
        emit gestureNotify(settingsValue("gestures/panLeftTitle").toString());
        iTerm->putString(settingsValue("gestures/panLeftCommand").toString(), true);
    }
    else if( gesture==PanRight ) {
        emit gestureNotify(settingsValue("gestures/panRightTitle").toString());
        iTerm->putString(settingsValue("gestures/panRightCommand").toString(), true);
    }
    else if( gesture==PanDown ) {
        emit gestureNotify(settingsValue("gestures/panDownTitle").toString());
        iTerm->putString(settingsValue("gestures/panDownCommand").toString(), true);
    }
    else if( gesture==PanUp ) {
        emit gestureNotify(settingsValue("gestures/panUpTitle").toString());
        iTerm->putString(settingsValue("gestures/panUpCommand").toString(), true);
    }
}

void Util::notifyText(QString text)
{
    emit gestureNotify(text);
}

void Util::copyTextToClipboard(QString str)
{
    QClipboard *cb = QGuiApplication::clipboard();

    cb->clear();
    cb->setText(str);
}

bool Util::terminalHasSelection()
{
    return !iTerm->selection().isNull();
}

bool Util::canPaste()
{

    QClipboard *cb = QGuiApplication::clipboard();

    return !cb->text().isEmpty();
}

void Util::selectionFinished()
{
    emit clipboardOrSelectionChanged();
}

//static
bool Util::charIsHexDigit(QChar ch)
{
    if (ch.isDigit()) // 0-9
        return true;
    else if (ch.toLatin1() >= 65 && ch.toLatin1() <= 70) // A-F
        return true;
    else if (ch.toLatin1() >= 97 && ch.toLatin1() <= 102) // a-f
        return true;

    return false;
}
