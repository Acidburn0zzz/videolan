/*****************************************************************************
 * ClipWorkflow.cpp : Clip workflow. Will extract a single frame from a VLCMedia
 *****************************************************************************
 * Copyright (C) 2008-2009 the VLMC team
 *
 * Authors: Hugo Beauzee-Luyssen <hugo@vlmc.org>
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

#include <QtDebug>

#include "vlmc.h"
#include "ClipWorkflow.h"
#include "Pool.hpp"

ClipWorkflow::ClipWorkflow( Clip::Clip* clip ) :
                m_clip( clip ),
                m_mediaPlayer(NULL),
                m_state( ClipWorkflow::Stopped ),
                m_requiredState( ClipWorkflow::None )
{
    for ( unsigned int i = 0; i < 5; ++i )
        m_availableBuffers.enqueue( new unsigned char[VIDEOHEIGHT * VIDEOWIDTH * 4] );
    m_stateLock = new QReadWriteLock;
    m_requiredStateLock = new QMutex;
    m_waitCond = new QWaitCondition;
    m_condMutex = new QMutex;
    m_initWaitCond = new WaitCondition;
    m_renderWaitCond = new WaitCondition;
    m_pausingStateWaitCond = new WaitCondition;
    m_pausedThreadCondWait = new WaitCondition;
    m_buffersLock = new QMutex;
}

ClipWorkflow::~ClipWorkflow()
{
    delete m_buffersLock;
    delete m_pausedThreadCondWait;
    delete m_pausingStateWaitCond;
    delete m_initWaitCond;
    delete m_condMutex;
    delete m_waitCond;
    delete m_requiredStateLock;
    delete m_stateLock;
    while ( m_buffers.empty() == false )
        delete[] m_availableBuffers.dequeue();
}

unsigned char*    ClipWorkflow::getOutput()
{
    QMutexLocker    lock( m_buffersLock );

    qDebug() << "Getting output";

    if ( m_buffers.isEmpty() == true )
        return NULL;
    unsigned char* buff = m_buffers.dequeue();
    m_availableBuffers.enqueue( buff );
    return buff;
}

void    ClipWorkflow::checkStateChange()
{
    QMutexLocker    lock( m_requiredStateLock );
    QWriteLocker    lock2( m_stateLock );
    if ( m_requiredState != ClipWorkflow::None )
    {
        m_state = m_requiredState;
        m_requiredState = ClipWorkflow::None;
        checkSynchronisation( m_state );
    }
}

void    ClipWorkflow::lock( ClipWorkflow* cw, void** pp_ret )
{
    qDebug() << "Computing new frame";
    cw->m_buffersLock->lock();
    unsigned char*  buff;
    if ( cw->m_availableBuffers.size() > 0 )
        buff = cw->m_availableBuffers.dequeue();
    else
        buff = new unsigned char[VIDEOHEIGHT * VIDEOWIDTH * 4];
    cw->m_buffers.enqueue( buff );
    *pp_ret = buff;
}

void    ClipWorkflow::unlock( ClipWorkflow* cw )
{
    cw->m_buffersLock->unlock();
    cw->m_stateLock->lockForWrite();

    if ( cw->m_state == Rendering )
    {
        QMutexLocker    lock( cw->m_condMutex );

        cw->m_state = Sleeping;
        cw->m_stateLock->unlock();

        cw->m_renderWaitCond->wake();
        cw->emit renderComplete( cw );

        cw->m_waitCond->wait( cw->m_condMutex );
        cw->m_stateLock->lockForWrite();
        cw->m_state = Rendering;
        cw->m_stateLock->unlock();
    }
    else if ( cw->m_state == Paused )
    {
        QMutexLocker    lock( cw->m_condMutex );

        cw->m_stateLock->unlock();
        cw->setState( ClipWorkflow::ThreadPaused );
        cw->m_pausedThreadCondWait->wake();
        cw->m_waitCond->wait( cw->m_condMutex );
        cw->setState( ClipWorkflow::Paused );
    }
    else
        cw->m_stateLock->unlock();
    cw->checkStateChange();
}

void    ClipWorkflow::setVmem()
{
    char        buffer[32];

    m_vlcMedia->addOption( ":no-audio" );
    m_vlcMedia->addOption( ":vout=vmem" );
    m_vlcMedia->setDataCtx( this );
    m_vlcMedia->setLockCallback( reinterpret_cast<LibVLCpp::Media::lockCallback>( &ClipWorkflow::lock ) );
    m_vlcMedia->setUnlockCallback( reinterpret_cast<LibVLCpp::Media::unlockCallback>( &ClipWorkflow::unlock ) );
    m_vlcMedia->addOption( ":vmem-chroma=RV24" );

    sprintf( buffer, ":vmem-width=%i", VIDEOWIDTH );
    m_vlcMedia->addOption( buffer );

    sprintf( buffer, ":vmem-height=%i", VIDEOHEIGHT );
    m_vlcMedia->addOption( buffer );

    sprintf( buffer, "vmem-pitch=%i", VIDEOWIDTH * 3 );
    m_vlcMedia->addOption( buffer );
}

void    ClipWorkflow::initialize()
{
    setState( Initializing );
    m_vlcMedia = new LibVLCpp::Media( m_clip->getParent()->getFileInfo()->absoluteFilePath() );
    setVmem();
    m_mediaPlayer = Pool<LibVLCpp::MediaPlayer>::getInstance()->get();
    m_mediaPlayer->setMedia( m_vlcMedia );

    connect( m_mediaPlayer, SIGNAL( playing() ), this, SLOT( setPositionAfterPlayback() ), Qt::DirectConnection );
    connect( m_mediaPlayer, SIGNAL( endReached() ), this, SLOT( clipEndReached() ), Qt::DirectConnection );
    m_mediaPlayer->play();
}

void    ClipWorkflow::setPositionAfterPlayback()
{
    disconnect( m_mediaPlayer, SIGNAL( playing() ), this, SLOT( setPositionAfterPlayback() ) );
    connect( m_mediaPlayer, SIGNAL( positionChanged() ), this, SLOT( pauseAfterPlaybackStarted() ), Qt::DirectConnection );
    m_mediaPlayer->setPosition( m_clip->getBegin() );
}

void    ClipWorkflow::pauseAfterPlaybackStarted()
{
    disconnect( m_mediaPlayer, SIGNAL( positionChanged() ), this, SLOT( pauseAfterPlaybackStarted() ) );
    disconnect( m_mediaPlayer, SIGNAL( playing() ), this, SLOT( pauseAfterPlaybackStarted() ) );

    connect( m_mediaPlayer, SIGNAL( paused() ), this, SLOT( initializedMediaPlayer() ), Qt::DirectConnection );
    m_mediaPlayer->pause();

}

void    ClipWorkflow::initializedMediaPlayer()
{
    disconnect( m_mediaPlayer, SIGNAL( paused() ), this, SLOT( initializedMediaPlayer() ) );
    setState( Ready );
}

bool    ClipWorkflow::isReady() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::Ready;
}

bool    ClipWorkflow::isPausing() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::Pausing;
}

bool    ClipWorkflow::isThreadPaused() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::ThreadPaused;
}

bool    ClipWorkflow::isEndReached() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::EndReached;
}

bool    ClipWorkflow::isStopped() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::Stopped;
}

ClipWorkflow::State     ClipWorkflow::getState() const
{
    return m_state;
}

void    ClipWorkflow::startRender()
{
    if ( isReady() == false )
        m_initWaitCond->wait();

    m_mediaPlayer->play();
    setState( Rendering );
}

void    ClipWorkflow::clipEndReached()
{
    setState( EndReached );
}

Clip*     ClipWorkflow::getClip()
{
    return m_clip;
}

void            ClipWorkflow::stop()
{
    if ( m_mediaPlayer )
    {
        m_mediaPlayer->stop();
        Pool<LibVLCpp::MediaPlayer>::getInstance()->release( m_mediaPlayer );
        disconnect( m_mediaPlayer, SIGNAL( endReached() ), this, SLOT( clipEndReached() ) );
        m_mediaPlayer = NULL;
        setState( Stopped );
        QMutexLocker    lock( m_requiredStateLock );
        m_requiredState = ClipWorkflow::None;
        delete m_vlcMedia;
    }
    else
        qDebug() << "ClipWorkflow has already been stopped";
}

void            ClipWorkflow::setPosition( float pos )
{
    m_mediaPlayer->setPosition( pos );
}

bool            ClipWorkflow::isRendering() const
{
    QReadLocker lock( m_stateLock );
    return m_state == ClipWorkflow::Rendering;
}

void            ClipWorkflow::checkSynchronisation( State newState )
{
    switch ( newState )
    {
        case Ready:
            m_initWaitCond->wake();
            break ;
        case Pausing:
            m_pausingStateWaitCond->wake();
            break ;
        default:
            break ;
    }
}

void            ClipWorkflow::setState( State state )
{
    {
        QWriteLocker    lock( m_stateLock );
//        qDebug() << "Changing from state" << m_state << "to state" << state;
        m_state = state;
    }
    checkSynchronisation( state );
}

void            ClipWorkflow::queryStateChange( State newState )
{
//    qDebug() << "Querying state change to" << newState;
    QMutexLocker    lock( m_requiredStateLock );
    m_requiredState = newState;
}

void            ClipWorkflow::wake()
{
    m_waitCond->wakeAll();
}

QReadWriteLock* ClipWorkflow::getStateLock()
{
    return m_stateLock;
}

void            ClipWorkflow::reinitialize()
{
    QWriteLocker    lock( m_stateLock );
    m_state = Stopped;
    queryStateChange( None );
}

void            ClipWorkflow::pause()
{
    setState( Paused );
    m_mediaPlayer->pause();
    QMutexLocker    lock( m_requiredStateLock );
    m_requiredState = ClipWorkflow::None;
}

void            ClipWorkflow::unpause( bool wakeRenderThread /*= true*/ )
{
    queryStateChange( ClipWorkflow::Rendering );
    m_mediaPlayer->pause();
    if ( wakeRenderThread == true )
    {
        wake();
    }
}

void        ClipWorkflow::waitForCompleteRender( bool dontCheckRenderStarted /*= false*/ )
{
    if ( isRendering() == true || dontCheckRenderStarted == true )
        m_renderWaitCond->wait();
}

void        ClipWorkflow::waitForCompleteInit()
{
    if ( isReady() == false )
        m_initWaitCond->wait();
}

void        ClipWorkflow::waitForPausingState()
{
    if ( isPausing() == false )
        m_pausingStateWaitCond->wait();
}

void        ClipWorkflow::waitForPausedThread()
{
    if ( isThreadPaused() == false )
        m_pausedThreadCondWait->wait();
}

QMutex*     ClipWorkflow::getSleepMutex()
{
    return m_condMutex;
}

LibVLCpp::MediaPlayer*       ClipWorkflow::getMediaPlayer()
{
    return m_mediaPlayer;
}
