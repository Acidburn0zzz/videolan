/*****************************************************************************
 * StackViewController.cpp
 *****************************************************************************
 * Copyright (C) 2008-2016 VideoLAN
 *
 * Authors: Thomas Boquet <thomas.boquet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "StackViewController.h"

StackViewController::StackViewController( QWidget* parent ) :
        QWidget( parent ), m_current( 0 )
{
    m_nav     = new StackViewNavController( this );
    m_layout  = new QVBoxLayout( this );
    m_controllerStack = new QStack<ViewController*>();

    connect( m_nav->previousButton(), SIGNAL( clicked() ),
                     this, SLOT( previous() ) );
    m_layout->addWidget( m_nav );
    parent->setLayout( m_layout );
}

StackViewController::~StackViewController()
{
    delete m_nav;
    delete m_controllerStack;
}

void
StackViewController::pushViewController( ViewController* viewController,
                                                 bool /*animated*/ )
{
//    animated = false;

    connect( viewController, SIGNAL( destroyed() ),
             this, SLOT( viewDestroyed() ) );
    if ( m_current )
    {
        m_layout->removeWidget( m_current->view() );
        m_current->view()->hide();
        m_controllerStack->push( m_current );

        m_nav->previousButton()->setHidden( false );
        m_nav->previousButton()->setText( "< " + m_current->title() );
    }

    m_current = viewController;
    m_nav->setTitle( m_current->title() );
    m_layout->insertWidget( 1, m_current->view() );
    if ( m_current->view()->isHidden() )
        m_current->view()->show();
    emit viewChanged( viewController );
}

void
StackViewController::restorePrevious()
{
    m_current = m_controllerStack->pop();

    m_nav->setTitle( m_current->title() );
    m_layout->insertWidget( 1, m_current->view() );
    m_current->view()->setHidden( false );

    if ( !m_controllerStack->size() )
        m_nav->previousButton()->setHidden( true );
    else
    {
       m_nav->previousButton()->setText( "< " +
       m_controllerStack->value( m_controllerStack->size() - 1 )->title() );
    }
    emit viewChanged( m_current );
}

void
StackViewController::viewDestroyed()
{
    Q_ASSERT( m_controllerStack->isEmpty() == false );

    if ( QObject::sender() == m_current )
    {
        restorePrevious();
    }
}

void
StackViewController::popViewController( bool /*animated*/ )
{
//    animated = false;

    if ( !m_controllerStack->size() )
        return ;

    m_layout->removeWidget( m_current->view() );
    m_current->view()->hide();
    delete m_current;
}

void
StackViewController::previous()
{
    popViewController();
}

