/*****************************************************************************
 * MainWorkflow.h : Will query all of the track workflows to render the final
 *                  image
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

#ifndef MAINWORKFLOW_H
#define MAINWORKFLOW_H

#include <QObject>
#include <QReadWriteLock>
#include <QMutex>

#include "tools/Toggleable.hpp"
#include "TrackWorkflow.h"
#include "tools/Singleton.hpp"

class   MainWorkflow : public QObject, public Singleton<MainWorkflow>
{
    Q_OBJECT

    public:
        MainWorkflow( int trackCount );
        ~MainWorkflow();

        void                    addClip( Clip* clip, unsigned int trackId, qint64 start );
        void                    startRender();
        void                    getOutput();
        unsigned char*          getSynchroneOutput();

        /**
         *  \brief              Set the workflow position
         *  \param              pos: The position in vlc position
        */
        void                    setPosition( float pos );

        /**
         *  \return             Returns the global length of the workflow
         *                      in frames.
        */
        qint64                  getLength() const;

        /**
         *  Returns the number of tracks in this workflow
         */
        unsigned int            getTrackCount() const;

        /**
         *  Stop the workflow (including sub track workflows and clip workflows)
         */
        void                    stop();

        /**
         *  Pause the main workflow and all its sub-workflows
         */
        void                    pause();

        static unsigned char*   blackOutput;
        void                    nextFrame();
        void                    previousFrame();

        void                    activateOneFrameOnly();
        
        static MainWorkflow*    getInstance();
        static void             deleteInstance();

        /**
         *  \brief  This method will wake every wait condition, so that threads won't
         *          be waiting anymore, thus avoiding dead locks.
         */
        void                    cancelSynchronisation();

    private:
        static MainWorkflow*    m_instance;
//        QAtomicInt              m_oneFrameOnly;

    private:
        void                    computeLength();

    private:
        Toggleable<TrackWorkflow*>*     m_tracks;
        qint64                          m_currentFrame;
        qint64                          m_length;
        unsigned int                    m_trackCount;
        /**
         *  This boolean describe is a render has been started
        */
        bool                            m_renderStarted;
        QReadWriteLock*                 m_renderStartedLock;

        QMutex*                         m_renderMutex;
        QAtomicInt                      m_nbTracksToPause;
        QAtomicInt                      m_nbTracksToRender;
        QMutex*                         m_highestTrackNumberMutex;
        unsigned int                    m_highestTrackNumber;
        unsigned char*                  m_synchroneRenderingBuffer;
        QWaitCondition*                 m_synchroneRenderWaitCondition;
        QMutex*                         m_synchroneRenderWaitConditionMutex;

    public slots:
        void                            clipMoved( QUuid, int, int, qint64 );

    private slots:
        void                            trackEndReached( unsigned int trackId );
        void                            trackPaused();
        void                            tracksRenderCompleted( unsigned int trackId );

    signals:
        /**
         *  \brief Used to notify a change to the timeline cursor
         */
        void                    frameChanged( qint64 currentFrame );
        /**
          * \brief Used to nofify a change to the PreviewWidget
          */
        void                    positionChanged( float pos );

        void                    mainWorkflowEndReached();
        void                    mainWorkflowPaused();
};

#endif // MAINWORKFLOW_H
