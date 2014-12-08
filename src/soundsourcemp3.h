/***************************************************************************
                          soundsourcemp3.h  -  description
                             -------------------
    begin                : Wed Feb 20 2002
    copyright            : (C) 2002 by Tue and Ken Haste Andersen
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SOUNDSOURCEMP3_H
#define SOUNDSOURCEMP3_H

#include "soundsource.h"

#include <errno.h>
#include <id3tag.h>
#ifdef _MSC_VER
  // So mad.h doesn't try to use inline assembly which MSVC doesn't support. 
  // Notably, FPM_64BIT does not require a 64-bit machine. It merely requires a 
  // compiler that supports 64-bit types.
  #define FPM_64BIT 
#endif
#include <mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <QFile>

#include <vector>

/**
  *@author Tue and Ken Haste Andersen
  */

class SoundSourceMp3 : public Mixxx::SoundSource {
    typedef SoundSource Super;

public:
    static QList<QString> supportedFileExtensions();

    explicit SoundSourceMp3(QString qFilename);
    ~SoundSourceMp3();

    Result parseHeader() /*override*/;
    QImage parseCoverArt() /*override*/;

    Result open() /*override*/;

    diff_type seekFrame(diff_type frameIndex) /*override*/;
    size_type readFrameSamplesInterleaved(size_type frameCount, sample_type* sampleBuffer) /*override*/;
    size_type readStereoFrameSamplesInterleaved(size_type frameCount, sample_type* sampleBuffer) /*override*/;

private:
    void close();

    size_type readFrameSamplesInterleaved(size_type frameCount, sample_type* sampleBuffer, bool readStereoSamples);

    /** Returns the position of the frame which was found. The found frame is set to
     * the current element in m_qSeekList */
    int findFrame(int pos);
    size_type discardFrames(size_type frameCount);

    QFile m_file;
    quint64 m_fileSize;
    unsigned char* m_pFileData;

    mad_stream m_madStream;

    /** Struct used to store mad frames for seeking */
    struct MadSeekFrameType {
        const unsigned char *pFrameData;
        long int pos;
    };

    /** It is not possible to make a precise seek in an mp3 file without decoding the whole stream.
     * To have precise seek within a limited range from the current decode position, we keep track
     * of past decodeded frame, and their exact position. If a seek occours and it is within the
     * range of frames we keep track of a precise seek occours, otherwise an unprecise seek is performed
     */
    typedef std::vector<MadSeekFrameType> MadSeekFrameList;
    MadSeekFrameList m_seekFrameList;
    int m_iAvgFrameSize;
    /** Index iterator for m_qSeekList. Helps us keep track of where we are in the file. */
    MadSeekFrameList::size_type m_currentSeekFrameIndex;

    inline
    const MadSeekFrameType* getSeekFrame(MadSeekFrameList::size_type frameIndex) const {
        if (m_seekFrameList.size() > frameIndex) {
            return &m_seekFrameList[frameIndex];
        } else {
            return NULL;
        }
    }

    // current play position
    mad_frame m_madFrame;
    mad_synth m_madSynth;
    unsigned short m_madSynthOffset; // left overs from the previous read
};

#endif
