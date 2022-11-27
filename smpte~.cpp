// smpte~ - Pd external for generating or decoding ltc audio timecode

// using libltc library by Robin Gareus.
// based on Max object by Mattijs Kneppers

// by Matthias Kronlachner
// www.matthiaskronlachner.com

// adapted for use with pdlibbuilder by Jean-Yves Gratius

#include "smpte~.hpp"

#include "ltc.h"
#include "decoder.h"
#include "encoder.h"
#include <math.h>
#include <string.h>
// #include <stddef.h>
#include <stdlib.h>
// #include <stdio.h>


#include "m_pd.h"


static t_class *smpte_tilde_class;
typedef struct _smpte_tilde {
	t_object obj;
	t_float x_f;  /* place to hold inlet's value if it's set by message */
 	t_outlet *outlet1;
  	t_outlet *outlet2;
	LTCEncoder		*encoder;
    LTCDecoder      *decoder;
    double			length; // in seconds
    double			fps;
    double			sampleRate;
    ltcsnd_sample_t *smpteBuffer;
    int				smpteBufferLength;
    int				smpteBufferTime;
    SMPTETimecode	startTimeCode;
    int				startTimeCodeChanged;
    int				autoIncrease;
    
    LTCFrameExt     frame;
    unsigned int    dec_bufpos;
    float           *dec_buffer; // has to contain 256 samples...
} t_smpte_tilde;


static t_int *smpte_tilde_perform(t_int *w) 
{
	t_smpte_tilde *x = (t_smpte_tilde *)(w[1]);
    
	t_sample *in = (t_sample *)(w[2]);
    t_sample *out = (t_sample *)(w[3]);
    int n = (int)(w[4]);
    
    // check if timecode signal at input
    bool ltc_input = false;
    
    float* input = in;
    
    // search for first non-zero sample
    
    for (int i = 0; i < n; i++) {
        if ((*input++) != 0.f) {
            ltc_input = true;
            break;
        }
    }
    
    if (ltc_input) { // get timecode from audio signal
        
        for (int i = 0; i < n; i++) {
            if (x->dec_bufpos > 255)
            {
                ltc_decoder_write_float(x->decoder, x->dec_buffer, 256, 0);
                x->dec_bufpos = 0;
            }
            
            x->dec_buffer[x->dec_bufpos++] = input[i];
        }
        
        
        while (ltc_decoder_read(x->decoder, &x->frame)) {
            SMPTETimecode stime;
            ltc_frame_to_time(&stime, &x->frame.ltc, 1);
            
            // output parts of timecode individually
            t_atom timecode_list[4];
            SETFLOAT(&timecode_list[0], stime.hours);
            SETFLOAT(&timecode_list[1], stime.mins);
            SETFLOAT(&timecode_list[2], stime.secs);
            SETFLOAT(&timecode_list[3], stime.frame);
            
            outlet_list(x->outlet2, &s_list, 4,timecode_list);
            
            // store timecode to buffer (if signal is lost stay at this value or increase from there)
            x->startTimeCode = stime;
            x->startTimeCodeChanged = 1;
            
            /*
             printf("%04d-%02d-%02d %s ",
             ((stime.years < 67) ? 2000+stime.years : 1900+stime.years),
             stime.months,
             stime.days,
             stime.timezone
             );
             printf("%02d:%02d:%02d%c%02d | %8lld %8lld%s\n",
             stime.hours,
             stime.mins,
             stime.secs,
             (frame.ltc.dfbit) ? '.' : ':',
             stime.frame,
             frame.off_start,
             frame.off_end,
             frame.reverse ? " R" : ""
             );
             */

        }
        
    }
    else // generate timecode
    {
        while (n--) {
            if (x->smpteBufferTime >= x->smpteBufferLength) {
                if (x->startTimeCodeChanged) {
                    ltc_encoder_set_timecode(x->encoder, &x->startTimeCode);
                    x->startTimeCodeChanged = 0;
                    
                }
                else if (x->autoIncrease) {
                    ltc_encoder_inc_timecode(x->encoder);
                }
                else {
                    //user apparently wants to keep using the same frame twice
                }
                
                SMPTETimecode st;
                ltc_encoder_get_timecode(x->encoder, &st);
                
                char timeString[256];
                sprintf(timeString, "%02d:%02d:%02d:%02d", st.hours, st.mins, st.secs, st.frame);
                
                // output parts of timecode individually
                t_atom timecode_list[4];
                SETFLOAT(&timecode_list[0], st.hours);
                SETFLOAT(&timecode_list[1], st.mins);
                SETFLOAT(&timecode_list[2], st.secs);
                SETFLOAT(&timecode_list[3], st.frame);
 				outlet_list(x->outlet2, &s_list,4,timecode_list);
                
                ltc_encoder_encode_frame(x->encoder);
                //ltc_encoder_get_bufptr is deprecated
               x->smpteBuffer = ltc_encoder_get_bufptr(x->encoder, &x->smpteBufferLength, 1);
            //   x->smpteBufferLength = ltc_encoder_get_bufferptr(x->encoder, &x->smpteBuffer , 1);
            // 	post ("len=%d",x->smpteBufferLength);   
                x->smpteBufferTime = 0;
            }
            
            *out++ = x->smpteBuffer[x->smpteBufferTime] / 128. - 1.;
            
            x->smpteBufferTime++;
        }
    }
   
	return (w+5);
}  






static void smpte_tilde_dsp(t_smpte_tilde *x, t_signal **sp) 
{
  dsp_add(smpte_tilde_perform,4,x, sp[0]->s_vec, sp[1]->s_vec, (t_int)sp[0]->s_n);
}

static void smpte_tilde_set_autoincrease(t_smpte_tilde *x,t_floatarg value)
{
    
    if (value <= 0) value = 0;
	if (value >= 1) value = 1;
	x->autoIncrease = (int)value;
    
}


static void smpte_tilde_set_time(t_smpte_tilde *x,t_symbol *s,int argc,t_atom *argv)
{
    if (argc != 4) {
        post("time: please pass a list with four numbers");
    }
    else
    {
        if (atom_getfloatarg(3,argc,argv) > x->fps)
        {
            post ("requested frame number higher than fps");
        } else {
            
            x->startTimeCode.hours = atom_getfloatarg(0,argc,argv);
            x->startTimeCode.mins = atom_getfloatarg(1,argc,argv);
            x->startTimeCode.secs = atom_getfloatarg(2,argc,argv);
            x->startTimeCode.frame = atom_getfloatarg(3,argc,argv);
            x->startTimeCodeChanged = 1;
        }
    } 
}

static void smpte_tilde_set_milliseconds(t_smpte_tilde *x,float f)
{
    double timeInSeconds = f / 1000.;
    double intPart = 0;
    double subSecond = modf(timeInSeconds, &intPart);
    
    x->startTimeCode.hours = timeInSeconds / 360;
    x->startTimeCode.mins = (int)(timeInSeconds / 60) % 60;
    x->startTimeCode.secs = (int)(timeInSeconds) % 60;
    x->startTimeCode.frame = (int)(subSecond * x->fps);
    
    x->startTimeCodeChanged = 1;
    // post("jumped to %i", f);
}

static void smpte_tilde_set_fps(t_smpte_tilde *x, t_floatarg fvalue)
{
    int value = (int)fvalue;
    switch (value) {
		case 0:
			x->fps = 24;
			break;
		case 1:
			x->fps = 25;
			break;
		case 2:
			x->fps = 29.97;
			break;
		case 3:
			x->fps = 30;
			break;
		default:
			break;
	}
	//JYG: Samplerate() remplacé par sys_getsr() cf d_ugen.c pour gestion en tenant compte de block~)
	
	//ltc_encoder_set_bufsize is deprecated
	ltc_encoder_set_buffersize(x->encoder, sys_getsr(), x->fps);
	ltc_encoder_reinit(x->encoder, sys_getsr(), x->fps, x->fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
    
}

// constructor
static void *smpte_tilde_new(t_symbol *s, int argc, t_atom *argv) 
{
	t_smpte_tilde *x = (t_smpte_tilde *)pd_new(smpte_tilde_class);
	x->outlet1 = outlet_new(&x->obj, gensym("signal"));
	x->outlet2 = outlet_new(&x->obj, gensym("list"));
    x->x_f = 0;
    x->fps=25.; 
    x->length=20.; 
    x->autoIncrease=1; 
    x->startTimeCodeChanged=1; 
    x->smpteBufferTime=0; 
    x->smpteBufferLength=0;
    
      // SMPTETimecode *st = (SMPTETimecode *)malloc(sizeof(SMPTETimecode));
    const char timezone[6] = "+0100";
    strcpy(x->startTimeCode.timezone, timezone);
    x->startTimeCode.years = 0;
    x->startTimeCode.months = 0;
    x->startTimeCode.days = 0;
    x->startTimeCode.hours = 0;
    x->startTimeCode.mins = 0;
    x->startTimeCode.secs = 0;
    x->startTimeCode.frame = 0;
    
    //startTimeCode = st;
    
    x->encoder = ltc_encoder_create(1, 1, LTC_TV_625_50, 0);
   // DEPRECATED 
  //  ltc_encoder_set_bufsize(x->encoder, sys_getsr(), x->fps);
    ltc_encoder_set_buffersize(x->encoder, sys_getsr(), x->fps);
    ltc_encoder_reinit(x->encoder, sys_getsr(), x->fps, x->fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
    
    ltc_encoder_set_filter(x->encoder, 0);
    ltc_encoder_set_filter(x->encoder, 25.0);
    ltc_encoder_set_volume(x->encoder, -3.0);
    
    // decoder
    int apv = sys_getsr()*1/25;
    
    x->dec_bufpos = 0;
        
    x->dec_buffer = (float*) malloc(sizeof(float)*256);// allocate buffer

    
    x->decoder = ltc_decoder_create(apv, 32);
    
    
       
    return (x); 
}    

void smpte_tilde_free(t_smpte_tilde *x) 
{
    ltc_decoder_free(x->decoder);
    ltc_encoder_free(x->encoder);
    free(x->dec_buffer); 
}

extern "C" {
void smpte_tilde_setup() 
{
	post("setting up smpte~");
	smpte_tilde_class = class_new(gensym("smpte~"),
                   (t_newmethod)smpte_tilde_new,
                   (t_method)smpte_tilde_free,
                   sizeof(t_smpte_tilde), CLASS_DEFAULT, A_GIMME, 0);

	/* this is magic to declare that the leftmost, "main" inlet
	takes signals; other signal inlets are done differently... */
	CLASS_MAINSIGNALIN(smpte_tilde_class, t_smpte_tilde, x_f);
 	class_addmethod(smpte_tilde_class, (t_method)smpte_tilde_dsp,
          gensym("dsp"), A_CANT, 0);

	class_addmethod(smpte_tilde_class, (t_method)smpte_tilde_set_fps,
          gensym("fps"), A_DEFFLOAT, 0);	    
	class_addmethod(smpte_tilde_class, (t_method)smpte_tilde_set_autoincrease,
          gensym("autoincrease"), A_DEFFLOAT, 0);	    
	class_addmethod(smpte_tilde_class, (t_method)smpte_tilde_set_time,
          gensym("time"), A_GIMME, 0);	    
	class_addmethod(smpte_tilde_class, (t_method)smpte_tilde_set_milliseconds,
          gensym("ms"), A_DEFFLOAT, 0);	
//    class_sethelpsymbol(smpte_tilde_class, gensym("smpte~-help.pd"));
    
  
    
} 

} //extern "C" 










