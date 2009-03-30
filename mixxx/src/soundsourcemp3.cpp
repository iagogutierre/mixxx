/***************************************************************************
                          soundsourcemp3.cpp  -  description
                             -------------------
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

#include "trackinfoobject.h"
#include "soundsourcemp3.h"
#include <QtDebug>


SoundSourceMp3::SoundSourceMp3(QString qFilename) : SoundSource(qFilename)
{
    QFile file( qFilename );
    if (!file.open(QIODevice::ReadOnly)) {
        //qDebug() << "MAD: Open failed:" << qFilename;
    }

    // Read the whole file into inputbuf:
    inputbuf_len = file.size();
    inputbuf = new char[inputbuf_len];
    unsigned int tmp = file.read(inputbuf, inputbuf_len);
    if (tmp != inputbuf_len) {
        // qDebug() << "MAD: ERR reading mp3-file: "
        //          << qFilename
        //          << "\nRead only "
        //          << tmp
        //          << "bytes, but wanted"
        //          << inputbuf_len
        //          << "bytes";
    }

    // Transfer it to the mad stream-buffer:
    mad_stream_init(&Stream);
    mad_stream_options(&Stream, MAD_OPTION_IGNORECRC);
    mad_stream_buffer(&Stream, (unsigned char *) inputbuf, inputbuf_len);

    /*
       Decode all the headers, and fill in stats:
     */
    mad_header Header;
    mad_header_init(&Header);
    filelength = mad_timer_zero;
    bitrate = 0;
    currentframe = 0;
    pos = mad_timer_zero;

    while ((Stream.bufend - Stream.this_frame) > 0)
    {
        if (mad_header_decode (&Header, &Stream) == -1) {
            if (!MAD_RECOVERABLE (Stream.error))
                break;
            if (Stream.error == MAD_ERROR_LOSTSYNC) {
                // ignore LOSTSYNC due to ID3 tags
                int tagsize = id3_tag_query (Stream.this_frame,Stream.bufend - Stream.this_frame);
                if (tagsize > 0) {
                    //qDebug() << "SSMP3::SSMP3() : skipping ID3 tag size " << tagsize;
                    mad_stream_skip (&Stream, tagsize);
                    continue;
                }
            }

            // qDebug() << "MAD: ERR decoding header "
            //          << currentframe << ": "
            //          << mad_stream_errorstr(&Stream)
            //          << " (len=" << mad_timer_count(filelength,MAD_UNITS_MILLISECONDS)
            //          << ")";
            continue;
        }

        // Grab data from Header

        // This warns us only when the reported sample rate changes. (and when it is first set)
        if(SRATE != Header.samplerate) {
            //qDebug() << "SSMP3() :: Setting SRATE to " << Header.samplerate << " from " << SRATE;
        }

        SRATE = Header.samplerate;
        m_iChannels = MAD_NCHANNELS(&Header);
        mad_timer_add (&filelength, Header.duration);
        bitrate += Header.bitrate;

        // Add frame to list of frames
        MadSeekFrameType * p = new MadSeekFrameType;
        p->m_pStreamPos = (unsigned char *)Stream.this_frame;
        p->pos = length();
        m_qSeekList.append(p);

        currentframe++;
    }
    //qDebug() << "channels " << m_iChannels;

    // Find average frame size
    if(currentframe)
        m_iAvgFrameSize = length()/currentframe;
    else
        m_iAvgFrameSize = 0;

    mad_header_finish (&Header); // This is a macro for nothing.

    if (currentframe==0)
        bitrate = 0;
    else
        bitrate = bitrate/currentframe;

    framecount = currentframe;
    currentframe = 0;
/*
    qDebug() << "length  = " << filelength.seconds << "d sec.";
    qDebug() << "frames  = " << framecount;
    qDebug() << "bitrate = " << bitrate/1000;
    qDebug() << "Size    = " << length();
 */

    Frame = new mad_frame;

    m_qSeekList.setAutoDelete(true);

    // Re-init buffer:
    seek(0);
}

SoundSourceMp3::~SoundSourceMp3()
{
    mad_stream_finish(&Stream);
    mad_frame_finish(Frame);
    mad_synth_finish(&Synth);
    delete [] inputbuf;

    m_qSeekList.clear();
}

long SoundSourceMp3::seek(long filepos)
{
    // Ensure that we are seeking to an even filepos
    Q_ASSERT(filepos%2==0);

//     qDebug() << "SEEK " << filepos;

    MadSeekFrameType * cur;

    if (filepos==0)
    {
        // Seek to beginning of file

        // Re-init buffer:
        mad_stream_finish(&Stream);
        mad_stream_init(&Stream);
        mad_stream_options(&Stream, MAD_OPTION_IGNORECRC);
        mad_stream_buffer(&Stream, (unsigned char *) inputbuf, inputbuf_len);
        mad_frame_init(Frame);
        mad_synth_init(&Synth);
        rest=-1;
        cur = m_qSeekList.at(0);
    }
    else
    {
        //qDebug() << "seek precise";
        // Perform precise seek accomplished by using a frame in the seek list

        // Find the frame to seek to in the list
        /*
           MadSeekFrameType *cur = m_qSeekList.last();
           int k=0;
           while (cur!=0 && cur->pos>filepos)
           {
            cur = m_qSeekList.prev();
         ++k;
           }
         */

        int framePos = findFrame(filepos);

//         qDebug() << "list length " << m_qSeekList.count();

        if (framePos==0 || framePos>filepos || m_qSeekList.at()<5)
        {
            //qDebug() << "Problem finding good seek frame (wanted " << filepos << ", got " << framePos << "), starting from 0";

            // Re-init buffer:
            mad_stream_finish(&Stream);
            mad_stream_init(&Stream);
            mad_stream_options(&Stream, MAD_OPTION_IGNORECRC);
            mad_stream_buffer(&Stream, (unsigned char *) inputbuf, inputbuf_len);
            mad_frame_init(Frame);
            mad_synth_init(&Synth);
            rest = -1;
            cur = m_qSeekList.first();
        }
        else
        {
//             qDebug() << "frame pos " << cur->pos;

            // Start four frame before wanted frame to get in sync...
            m_qSeekList.prev();
            m_qSeekList.prev();
            m_qSeekList.prev();
            cur = m_qSeekList.prev();

            // Start from the new frame
            mad_stream_finish(&Stream);
            mad_stream_init(&Stream);
            mad_stream_options(&Stream, MAD_OPTION_IGNORECRC);
//        qDebug() << "mp3 restore " << cur->m_pStreamPos;
            mad_stream_buffer(&Stream, (const unsigned char *)cur->m_pStreamPos, inputbuf_len-(long int)(cur->m_pStreamPos-(unsigned char *)inputbuf));

            // Mute'ing is done here to eliminate potential pops/clicks from skipping
            // Rob Leslie explains why here:
            // http://www.mars.org/mailman/public/mad-dev/2001-August/000321.html
            mad_synth_mute(&Synth);
            mad_frame_mute(Frame);

            // Decode the three frames before
            mad_frame_decode(Frame,&Stream);
            mad_frame_decode(Frame,&Stream);
            mad_frame_decode(Frame,&Stream);
            mad_frame_decode(Frame,&Stream);

            // this is also explained in the above mad-dev post
            mad_synth_frame(&Synth, Frame);

            // Set current position
            rest = -1;
            m_qSeekList.next();
            m_qSeekList.next();
            m_qSeekList.next();
            cur = m_qSeekList.next();
        }

        // Synthesize the the samples from the frame which should be discard to reach the requested position
        if (cur) //the "if" prevents crashes on bad files.
            discard(filepos-cur->pos);
    }
/*
    else
    {
        qDebug() << "seek unprecise";
        // Perform seek which is can not be done precise because no frames is in the seek list

        int newpos = (int)(inputbuf_len * ((float)filepos/(float)length()));
   //        qDebug() << "Seek to " << filepos << " " << inputbuf_len << " " << newpos;

        // Go to an approximate position:
        mad_stream_buffer(&Stream, (unsigned char *) (inputbuf+newpos), inputbuf_len-newpos);
        mad_synth_mute(&Synth);
        mad_frame_mute(Frame);

        // Decode a few (possible wrong) buffers:
        int no = 0;
        int succesfull = 0;
        while ((no<10) && (succesfull<2))
        {
            if (!mad_frame_decode(Frame, &Stream))
            succesfull ++;
            no ++;
        }

        // Discard the first synth:
        mad_synth_frame(&Synth, Frame);

        // Remaining samples in buffer are useless
        rest = -1;

        // Reset seek frame list
        m_qSeekList.clear();
        MadSeekFrameType *p = new MadSeekFrameType;
        p->m_pStreamPos = (unsigned char*)Stream.this_frame;
        p->pos = filepos;
        m_qSeekList.append(p);
        m_iSeekListMinPos = filepos;
        m_iSeekListMaxPos = filepos;
        m_iCurFramePos = filepos;
    }
 */

    // Unfortunately we don't know the exact fileposition. The returned position is thus an
    // approximation only:
    return filepos;

}

inline long unsigned SoundSourceMp3::length()
{
    enum mad_units units;

    //qDebug() << "SRATE: " << SRATE;
    switch (SRATE)
    {
    case 8000:
        units = MAD_UNITS_8000_HZ;
        break;
    case 11025:
        units = MAD_UNITS_11025_HZ;
        break;
    case 12000:
        units = MAD_UNITS_12000_HZ;
        break;
    case 16000:
        units = MAD_UNITS_16000_HZ;
        break;
    case 22050:
        units = MAD_UNITS_22050_HZ;
        break;
    case 24000:
        units = MAD_UNITS_24000_HZ;
        break;
    case 32000:
        units = MAD_UNITS_32000_HZ;
        break;
    case 44100:
        units = MAD_UNITS_44100_HZ;
        break;
    case 48000:
        units = MAD_UNITS_48000_HZ;
        break;
    default:             //By the MP3 specs, an MP3 _has_ to have one of the above samplerates...
        units = MAD_UNITS_44100_HZ;
        qDebug() << "Warning: MP3 with corrupt samplerate (" << SRATE << "), defaulting to 44100";

        SRATE = 44100; //Prevents division by zero errors.
    }

    return (long unsigned) 2 *mad_timer_count(filelength, units);
}

/*
  decode the chosen number of samples and discard
*/

unsigned long SoundSourceMp3::discard(unsigned long samples_wanted)
{
    unsigned long Total_samples_decoded = 0;
    int no;

    if(rest > 0)
        Total_samples_decoded += 2*(Synth.pcm.length-rest);

    while (Total_samples_decoded < samples_wanted)
    {
        if(mad_frame_decode(Frame,&Stream))
        {
            if(MAD_RECOVERABLE(Stream.error))
            {
                continue;
            } else if(Stream.error==MAD_ERROR_BUFLEN) {
                break;
            } else {
                break;
            }
        }
        mad_synth_frame(&Synth,Frame);
        no = math_min(Synth.pcm.length,(samples_wanted-Total_samples_decoded)/2);
        Total_samples_decoded += 2*no;
    }

    if (Synth.pcm.length > no)
	rest = no;
    else
	rest = -1;


    return Total_samples_decoded;
}

/*
   read <size> samples into <destination>, and return the number of
   samples actually read.
 */
unsigned SoundSourceMp3::read(unsigned long samples_wanted, const SAMPLE * _destination)
{
    // Ensure that we are reading an even number of samples. Otherwise this function may
    // go into an infinite loop
    Q_ASSERT(samples_wanted%2==0);
//     qDebug() << "frame list " << m_qSeekList.count();

    SAMPLE * destination = (SAMPLE *)_destination;
    unsigned Total_samples_decoded = 0;
    int i;

    // If samples are left from previous read, then copy them to start of destination
    // Make sure to take into account the case where there are more samples left over 
    // from the previous read than the client requested.
    if (rest > 0)
    {
        for (i=rest; i<Synth.pcm.length && Total_samples_decoded < samples_wanted; i++)
        {
            // Left channel
            *(destination++) = madScale(Synth.pcm.samples[0][i]);

            /* Right channel. If the decoded stream is monophonic then
            * the right output channel is the same as the left one. */
            if (m_iChannels>1)
                *(destination++) = madScale(Synth.pcm.samples[1][i]);
            else
                *(destination++) = madScale(Synth.pcm.samples[0][i]);

            // This is safe because we have Q_ASSERTed that samples_wanted is even.
            Total_samples_decoded += 2;

        }

        if(Total_samples_decoded >= samples_wanted) {
            if(i < Synth.pcm.length)
                rest = i;
            else
                rest = -1;
            return Total_samples_decoded;
        }

    }

//     qDebug() << "Decoding";
    int no = 0;
    unsigned int frames = 0;
    while (Total_samples_decoded < samples_wanted)
    {
        // qDebug() << "no " << Total_samples_decoded;
        if(mad_frame_decode(Frame,&Stream))
        {
            if(MAD_RECOVERABLE(Stream.error))
            {
                if(Stream.error == MAD_ERROR_LOSTSYNC) {
                    // Ignore LOSTSYNC due to ID3 tags
                    int tagsize = id3_tag_query(Stream.this_frame, Stream.bufend - Stream.this_frame);
                    if(tagsize > 0) {
                        //qDebug() << "SSMP3::Read Skipping ID3 tag size: " << tagsize;
                        mad_stream_skip(&Stream, tagsize);
                    }
                    continue;
                }
                //qDebug() << "MAD: Recoverable frame level ERR (" << mad_stream_errorstr(&Stream) << ")";
                continue;
            } else if(Stream.error==MAD_ERROR_BUFLEN) {
                // qDebug() << "MAD: buflen ERR";
                break;
            } else {
                // qDebug() << "MAD: Unrecoverable frame level ERR (" << mad_stream_errorstr(&Stream) << ").";
                break;
            }
        }

        ++frames;

        /* Once decoded the frame is synthesized to PCM samples. No ERRs
         * are reported by mad_synth_frame();
         */
        mad_synth_frame(&Synth,Frame);

        // Number of channels in frame
        //ch = MAD_NCHANNELS(&Frame->header);

        /* Synthesized samples must be converted from mad's fixed
         * point number to the consumer format (16 bit). Integer samples
         * are temporarily stored in a buffer that is flushed when
         * full.
         */


//         qDebug() << "synthlen " << Synth.pcm.length << ", remain " << (samples_wanted-Total_samples_decoded);
        no = math_min(Synth.pcm.length,(samples_wanted-Total_samples_decoded)/2);
        for (i=0; i<no; i++)
        {
            // Left channel
            *(destination++) = madScale(Synth.pcm.samples[0][i]);

            /* Right channel. If the decoded stream is monophonic then
            * the right output channel is the same as the left one. */
            if (m_iChannels==2)
                *(destination++) = madScale(Synth.pcm.samples[1][i]);
            else
                *(destination++) = madScale(Synth.pcm.samples[0][i]);
        }
        Total_samples_decoded += 2*no;

        // qDebug() << "decoded: " << Total_samples_decoded << ", wanted: " << samples_wanted;
    }

    // If samples are still left in buffer, set rest to the index of the unused samples
    if (Synth.pcm.length > no)
        rest = no;
    else
        rest = -1;

    // qDebug() << "decoded " << Total_samples_decoded << " samples in " << frames << " frames, rest: " << rest << ", chan " << m_iChannels;
    return Total_samples_decoded;
}

int SoundSourceMp3::ParseHeader(TrackInfoObject * Track)
{
    QString location = Track->getLocation();

    QFile sizetest( location );
    if (sizetest.size() == 0) {
        return ERR;
    }

    Track->setType("mp3");

    // mad-dev post in 2002-25-Jan on how to use libid3 by Rob Leslie
    // http://www.mars.org/mailman/public/mad-dev/2002-January/000439.html
    id3_file * fh = id3_file_open(qstrdup(location.toLocal8Bit()), ID3_FILE_MODE_READONLY);
    if (fh!=0)
    {
        id3_tag * tag = id3_file_tag(fh);
        if (tag!=0)
        {
            // Frame names can be found here:
            // http://www.id3.org/id3v2.4.0-frames.txt
            QString s;
            getField(tag,"TIT2",&s); // TIT2 : Title
            Track->setTitle(s);
            s="";
            getField(tag,"TPE1",&s); // TPE1 : Artist
            Track->setArtist(s);
            s="";
            getField(tag,"TBPM",&s); // TBPM: the bpm
            float bpm = 0;
            if (s.length()>1) bpm = str2bpm(s);
            if(bpm > 0) {
                Track->setBpm(bpm);
                Track->setBpmConfirm(true);
            }
            Track->setHeaderParsed(true);

            /*
               // On some tracks this segfaults. TLEN is very seldom used anyway...
               QString dur;
               getField(tag,"TLEN",&dur);
               if (dur.length()>0)
                Track->m_iDuration = dur.toInt();
             */
        }
        // this closes 'tag' for us
        id3_file_close(fh);
    }

    // Get file length. This has to be done by one of these options:
    // 1) looking for the tag named TLEN (above),
    //   -- too buggy to rely on
    // 2) See if the first frame contains a Xing header to get frame count
    //   -- Xing is non-Free, so we can't
    // 3) Find out if it is a variable frame size file by looking at the
    //    size of the first n frames. If constant size, estimate frame number
    //    from one frame size and file length in bytes
    //   -- We do this, 7/2008
    // 4) Count all the frames (slooow)
    //   -- We do not do this, 7/2008

    // Open file, initialize MAD and read beginnning of file

    QFile file(location);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "SSMP3::ParseHeader Open failed:" << location;
        return ERR;
    }

    mad_stream Stream;
    mad_header Header;

    mad_stream_init(&Stream);
    mad_stream_options(&Stream, MAD_OPTION_IGNORECRC);
    mad_header_init(&Header);

    // Stats we record as we go
    mad_timer_t dur = mad_timer_zero;
    unsigned long bitrate = 0;
    unsigned long bytesperframe = 0;
    bool constantbitrate = true;
    unsigned int frames = 0;

    // Number of bytes to read at a time to determine duration
    const unsigned int READLENGTH = 5000;
    char *inputbuf = new char[READLENGTH];
    const unsigned int DESIRED_FRAMES = 10;
    while(frames < DESIRED_FRAMES) {

        unsigned int readbytes = file.read(inputbuf, READLENGTH);
        if(readbytes != READLENGTH) {
            //qDebug() << "MAD: ERR reading mp3-file:" << location << "\nRead only" << readbytes << "bytes, but wanted" << READLENGTH << "bytes";
            if(readbytes == -1) {
                // fatal error, no bytes were read
                qDebug() << "MAD: fatal error reading mp3 file";
                delete inputbuf; // don't leak memory!
                return ERR;
            } else if(readbytes == 0) {
                // EOF, just break out of the loop
                break;
            }
            // otherwise we just have less data to work with, keep going!
        }

        // This preserves skiplen, so if we had a buffer error earlier
        // the skip will occur when we give it more data.
        mad_stream_buffer(&Stream, (unsigned char *) inputbuf, readbytes);

        while((Stream.bufend - Stream.this_frame) > 0) {
            if(mad_header_decode(&Header,&Stream) == -1) {
                if(!MAD_RECOVERABLE(Stream.error)) {
                    if(Stream.error == MAD_ERROR_BUFLEN) {
                        // We skipped too much, that's ok.
                        break;
                    }
                    // Something worse happened
                    //qDebug() << "SSMP3::ParseHeaeder - MAD unrecoverable error: " << mad_stream_errorstr(&Stream);
                    break;
                } else if(Stream.error == MAD_ERROR_LOSTSYNC) {
                    // ignore LOSTSYNC due to ID3 tags
                    int tagsize = id3_tag_query(Stream.this_frame, Stream.bufend - Stream.this_frame);
                    if (tagsize > 0) {
                        //qDebug() << "SSMP3::ParseHeader Skipping ID3 tag size: " << tagsize;
                        mad_stream_skip(&Stream, tagsize);
                        continue;
                    }
                }
                continue;
            }
            if (frames==0) {
                bitrate = Header.bitrate;
                dur = Header.duration;
                bytesperframe = (Stream.next_frame - Stream.this_frame);
            } else if (bitrate != Header.bitrate) {
                constantbitrate = false;
            }
            frames++;
        }

    }

    //qDebug() << "SSMP3::ParseHeader - frames read: " << frames << " bitrate " << Header.bitrate/1000;
    //qDebug() << "SSMP3::ParseHeader - samplerate " << Header.samplerate << " channels " << MAD_NCHANNELS(&Header);
    
    if (constantbitrate && frames>0) {
        // This means that duration is an approximation.
        // We take the duration of one frame and multiply it by
        // the number of frames we think can fit in the file size.
        
        // duration per frame * file_length bytes / (bytes per frame) = duration
        //mad_timer_multiply(&dur, Track->getLength()/((Stream.this_frame-Stream.buffer)/frames));
        mad_timer_multiply(&dur, Track->getLength()/bytesperframe);
        int duration = mad_timer_count(dur, MAD_UNITS_SECONDS);
        //qDebug() << "SSMP3::ParseHeader - CBR bytes per frame" << bytesperframe << " Estimated duration " << duration;
        Track->setDuration(duration);
        Track->setBitrate(Header.bitrate/1000);
    }


    Track->setSampleRate(Header.samplerate);
    Track->setChannels(MAD_NCHANNELS(&Header));

    mad_stream_finish(&Stream);
    delete [] inputbuf;
    file.close();
    return OK;

}

void SoundSourceMp3::getField(id3_tag * tag, const char * frameid, QString * str)
{
    // fetches the 0th frame from tag with frameid
    id3_frame * frame = id3_tag_findframe(tag, frameid, 0);
    if (frame)
    {
        // Unicode handling
        if (id3_field_getnstrings(&frame->fields[1])>0)
        {
            id3_utf16_t * framestr = id3_ucs4_utf16duplicate(id3_field_getstrings(&frame->fields[1], 0));
            int strlen = 0; while (framestr[strlen]!=0) strlen++;
            if (strlen>0)
                str->setUtf16((ushort *)framestr,strlen);
            free(framestr);
        }
    }
}

int SoundSourceMp3::findFrame(int pos)
{
    // Guess position of frame in m_qSeekList based on average frame size
    MadSeekFrameType * temp = m_qSeekList.at(math_min(m_qSeekList.count()-1, m_iAvgFrameSize ? (unsigned int)(pos/m_iAvgFrameSize) : 0));

/*
    if (temp!=0)
        qDebug() << "find " << pos << ", got " << temp->pos;
    else
        qDebug() << "find " << pos << ", tried idx " << math_min(m_qSeekList.count()-1 << ", total " << pos/m_iAvgFrameSize);
 */

    // Ensure that the list element is not at a greater position than pos
    while (temp!=0 && temp->pos>pos)
    {
        temp = m_qSeekList.prev();
//        if (temp!=0) qDebug() << "backing " << pos << ", got " << temp->pos;
    }

    // Ensure that the following position is also not smaller than pos
    if (temp!=0)
    {
        temp = m_qSeekList.current();
        while (temp!=0 && temp->pos<pos)
        {
            temp = m_qSeekList.next();
//            if (temp!=0) qDebug() << "fwd'ing " << pos << ", got " << temp->pos;
        }

        if (temp==0)
            temp = m_qSeekList.last();
        else
            temp = m_qSeekList.prev();
    }

    if (temp>0)
    {
//        qDebug() << "ended at " << pos << ", got " << temp->pos;
        return temp->pos;
    }
    else
    {
//        qDebug() << "ended at 0";
        return 0;
    }
}

inline signed int SoundSourceMp3::madScale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));

    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;

    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

