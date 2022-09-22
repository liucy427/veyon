/*
 * VncViewWidget.cpp - VNC viewer widget
 *
 * Copyright (c) 2006-2022 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include "VeyonConnection.h"
#include "VncConnection.h"
#include "VncViewWidget.h"


VncViewWidget::VncViewWidget( const ComputerControlInterface::Pointer& computerControlInterface,
							  QRect viewport, QWidget* parent ) :
	QWidget( parent ),
	VncView( computerControlInterface )
{
	setViewport( viewport );

	connectUpdateFunctions( this );

	connect( connection(), &VncConnection::stateChanged, this, &VncViewWidget::updateConnectionState );
	connect( &m_busyIndicatorTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::repaint) );

	// set up mouse border signal timer
	m_mouseBorderSignalTimer.setSingleShot( true );
	m_mouseBorderSignalTimer.setInterval( MouseBorderSignalDelay );
	connect( &m_mouseBorderSignalTimer, &QTimer::timeout, this, &VncViewWidget::mouseAtBorder );

	// set up background color
	if( parent == nullptr )
	{
		parent = this;
	}
	QPalette pal = parent->palette();
	pal.setColor( parent->backgroundRole(), Qt::black );
	parent->setPalette( pal );

	show();

	setFocusPolicy( Qt::WheelFocus );
	setFocus();

	setAttribute( Qt::WA_OpaquePaintEvent );
	installEventFilter( this );

	setMouseTracking( true );

	updateConnectionState();
}



VncViewWidget::~VncViewWidget()
{
	// do not receive any signals during connection shutdown
	connection()->disconnect( this );
}



QSize VncViewWidget::sizeHint() const
{
	QSize availableSize{QGuiApplication::primaryScreen()->availableVirtualSize()};
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
	const auto* windowScreen = windowHandle() ? windowHandle()->screen() : nullptr;
#else
	const auto* windowScreen = screen();
#endif
	if (windowScreen)
	{
		availableSize = windowScreen->availableVirtualSize();
	}

	availableSize -= window()->frameSize() - window()->size();

	const auto size = effectiveFramebufferSize();
	if (size.isEmpty())
	{
		return availableSize;
	}

	if (size.width() > availableSize.width() ||
		size.height() > availableSize.height())
	{
		return size.scaled(availableSize, Qt::KeepAspectRatio);
	}

	return size;
}



void VncViewWidget::setViewOnly( bool enabled )
{
	if( enabled == viewOnly() )
	{
		return;
	}

	if( enabled )
	{
		releaseKeyboard();
	}
	else
	{
		grabKeyboard();
	}

	VncView::setViewOnly( enabled );
}



void VncViewWidget::updateView( int x, int y, int w, int h )
{
	update( x, y, w, h );
}



QSize VncViewWidget::viewSize() const
{
	return size();
}



void VncViewWidget::setViewCursor(const QCursor& cursor)
{
	setCursor( cursor );
}



void VncViewWidget::updateGeometry()
{
	resize( effectiveFramebufferSize() );

	Q_EMIT sizeHintChanged();
}



bool VncViewWidget::event( QEvent* event )
{
	return VncView::handleEvent( event ) || QWidget::event( event );
}



bool VncViewWidget::eventFilter( QObject* obj, QEvent* event )
{
	if( viewOnly() )
	{
		if( event->type() == QEvent::KeyPress ||
			event->type() == QEvent::KeyRelease ||
			event->type() == QEvent::MouseButtonDblClick ||
			event->type() == QEvent::MouseButtonPress ||
			event->type() == QEvent::MouseButtonRelease ||
			event->type() == QEvent::Wheel )
		{
			return true;
		}
	}

	return QWidget::eventFilter( obj, event );
}



void VncViewWidget::focusInEvent( QFocusEvent* event )
{
	if( m_viewOnlyFocus == false )
	{
		setViewOnly( false );
	}

	QWidget::focusInEvent( event );
}



void VncViewWidget::focusOutEvent( QFocusEvent* event )
{
	m_viewOnlyFocus = viewOnly();

	if( viewOnly() == false )
	{
		setViewOnly( true );
	}

	QWidget::focusOutEvent( event );
}



void VncViewWidget::mouseEventHandler( QMouseEvent* event )
{
	if( event == nullptr )
	{
		return;
	}

	VncView::mouseEventHandler( event );

	if( event->type() == QEvent::MouseMove )
	{
		if( event->pos().y() == 0 )
		{
			if( m_mouseBorderSignalTimer.isActive() == false )
			{
				m_mouseBorderSignalTimer.start();
			}
		}
		else
		{
			m_mouseBorderSignalTimer.stop();
		}
	}
}



void VncViewWidget::paintEvent( QPaintEvent* paintEvent )
{
	QPainter p( this );
	p.setRenderHint( QPainter::SmoothPixmapTransform );

	const auto& image = connection()->image();

	if( image.isNull() || image.format() == QImage::Format_Invalid )
	{
		p.fillRect( paintEvent->rect(), Qt::black );
		drawBusyIndicator( &p );
		return;
	}

	auto source = viewport();
	if( source.isNull() || source.isEmpty() )
	{
		source = { QPoint{ 0, 0 }, image.size() };
	}

	if( isScaledView() )
	{
		// repaint everything in scaled mode to avoid artifacts at rectangle boundaries
		p.drawImage( QRect( QPoint( 0, 0 ), scaledSize() ), image, source );
	}
	else
	{
		p.drawImage( { 0, 0 }, image, source );
	}

	if( connection()->state() != VncConnection::State::Connected )
	{
		drawBusyIndicator( &p );
	}

	// draw black borders if neccessary
	const int screenWidth = scaledSize().width();
	if( screenWidth < width() )
	{
		p.fillRect( screenWidth, 0, width() - screenWidth, height(), Qt::black );
	}

	const int screenHeight = scaledSize().height();
	if( screenHeight < height() )
	{
		p.fillRect( 0, screenHeight, width(), height() - screenHeight, Qt::black );
	}
}



void VncViewWidget::resizeEvent( QResizeEvent* event )
{
	update();

	updateLocalCursor();

	QWidget::resizeEvent( event );
}



void VncViewWidget::drawBusyIndicator( QPainter* painter )
{
	static constexpr int BusyIndicatorSize = 100;
	static constexpr int BusyIndicatorSpeed = 5;

	QRect drawingRect{
		( width() - BusyIndicatorSize ) / 2,
		( height() - BusyIndicatorSize ) / 2,
		BusyIndicatorSize, BusyIndicatorSize,
		};

	QColor color(QStringLiteral("#00acdc"));
	QConicalGradient gradient;
	gradient.setCenter(drawingRect.center());
	gradient.setAngle((360 - m_busyIndicatorState) % 360);
	gradient.setColorAt(0, color);
	color.setAlpha(0);
	gradient.setColorAt(0.75, color);
	color.setAlpha(255);
	gradient.setColorAt(1, color);

	QPen pen(QBrush(gradient), 20);
	pen.setCapStyle(Qt::RoundCap);
	painter->setPen(pen);

	painter->setRenderHint(QPainter::Antialiasing);
	painter->drawArc( drawingRect,
					  ( 360 - ( m_busyIndicatorState % 360 ) ) * 16, 270 * 16 );

	m_busyIndicatorState += BusyIndicatorSpeed;
}



void VncViewWidget::updateConnectionState()
{
	if( connection()->state() != VncConnection::State::Connected )
	{
		m_busyIndicatorTimer.start( BusyIndicatorUpdateInterval );
	}
	else
	{
		m_busyIndicatorTimer.stop();

		resize( effectiveFramebufferSize() );
	}
}
