  /* -*- c++ -*- */
/*
 * Copyright 2006,2007,2008,2010 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <digital_ofdm_frame_acquisition.h>
#include <gr_io_signature.h>
#include <gr_expj.h>
#include <gr_math.h>
#include <cstdio>

#include <gr_sincos.h>
#include <math.h>
#include <boost/math/special_functions/trunc.hpp>

// apurv++ starts - for logging //
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>
#include <stdio.h>

#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef O_BINARY
#define OUR_O_BINARY O_BINARY
#else
#define OUR_O_BINARY 0
#endif

#ifdef O_LARGEFILE
#define OUR_O_LARGEFILE O_LARGEFILE
#else
#define OUR_O_LARGEFILE 0
#endif
// apurv++ ends - for logging //

#define VERBOSE 0
#define M_TWOPI (2*M_PI)
#define MAX_NUM_SYMBOLS 1000


static const pmt::pmt_t SYNC_TIME = pmt::pmt_string_to_symbol("sync_time");

digital_ofdm_frame_acquisition_sptr
digital_make_ofdm_frame_acquisition (unsigned int occupied_carriers, unsigned int fft_length, 
				unsigned int cplen,
				const std::vector<std::vector<gr_complex> > &known_symbol,
				//const std::vector<gr_complex> &known_symbol,
				unsigned int max_fft_shift_len)
{
  return gnuradio::get_initial_sptr(new digital_ofdm_frame_acquisition (occupied_carriers, fft_length, cplen,
									known_symbol, max_fft_shift_len));
}

digital_ofdm_frame_acquisition::digital_ofdm_frame_acquisition (unsigned occupied_carriers, unsigned int fft_length, 
						      unsigned int cplen,
						      const std::vector<std::vector<gr_complex> > &known_symbol,
						      //const std::vector<gr_complex> &known_symbol,
						      unsigned int max_fft_shift_len)
  : gr_block ("ofdm_frame_acquisition",
	      //gr_make_io_signature2 (2, 2, sizeof(gr_complex)*fft_length, sizeof(char)*fft_length),   // apurv--: input
	      //gr_make_io_signature2 (2, 2, sizeof(gr_complex)*occupied_carriers, sizeof(char))),	// apurv--: output
	      gr_make_io_signature3 (2, 3, sizeof(gr_complex)*fft_length, sizeof(char)*fft_length, sizeof(gr_complex)*fft_length),	// apurv++: input
	      gr_make_io_signature4 (2, 4, sizeof(gr_complex)*occupied_carriers, sizeof(char), sizeof(gr_complex)*occupied_carriers, sizeof(gr_complex)*fft_length)),	// apurv++: output
    d_occupied_carriers(occupied_carriers),
    d_fft_length(fft_length),
    d_cplen(cplen),
    d_freq_shift_len(max_fft_shift_len),
    d_known_symbol(known_symbol),
    d_coarse_freq(0),
    d_phase_count(0),
    // apurv++ start //
    d_fd(0),   
    d_fp(NULL),  
    d_boot_up(false), 
    d_file_opened(false)
    // apurv++ end //
{
  d_symbol_phase_diff.resize(d_fft_length);
  d_known_phase_diff.resize(d_occupied_carriers);
  //d_hestimate.resize(d_occupied_carriers);
  d_hest_freq_prod.resize(d_occupied_carriers); //apurv++

  unsigned int i = 0, j = 0;

  const std::vector<gr_complex> &first = d_known_symbol[0];
  std::fill(d_known_phase_diff.begin(), d_known_phase_diff.end(), 0);
  for(i = 0; i < first.size()-2; i+=2) {
    d_known_phase_diff[i] = norm(first[i] - first[i+2]);
  }
  
  d_phase_lut = new gr_complex[(2*d_freq_shift_len+1) * MAX_NUM_SYMBOLS];
  for(i = 0; i <= 2*d_freq_shift_len; i++) {
    for(j = 0; j < MAX_NUM_SYMBOLS; j++) {
      d_phase_lut[j + i*MAX_NUM_SYMBOLS] =  gr_expj(-M_TWOPI*d_cplen/d_fft_length*(i-d_freq_shift_len)*j);
    }
  }

  d_hestimate = (gr_complex*) malloc(sizeof(gr_complex) * d_occupied_carriers);
  memset(d_hestimate, 0, sizeof(gr_complex) * d_occupied_carriers);
}

digital_ofdm_frame_acquisition::~digital_ofdm_frame_acquisition(void)
{
  delete [] d_phase_lut;
  free(d_hestimate);
}

void
digital_ofdm_frame_acquisition::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
  unsigned ninputs = ninput_items_required.size ();
  for (unsigned i = 0; i < ninputs; i++)
    ninput_items_required[i] = 1;
}

gr_complex
digital_ofdm_frame_acquisition::coarse_freq_comp(float freq_delta, int symbol_count)
{
  //  return gr_complex(cos(-M_TWOPI*freq_delta*d_cplen/d_fft_length*symbol_count),
  //	    sin(-M_TWOPI*freq_delta*d_cplen/d_fft_length*symbol_count));

  return gr_expj((float)(-M_TWOPI*freq_delta*d_cplen)/d_fft_length*symbol_count);

  //return d_phase_lut[MAX_NUM_SYMBOLS * (d_freq_shift_len + freq_delta) + symbol_count];
}

void
digital_ofdm_frame_acquisition::correlate(const gr_complex *symbol, int zeros_on_left)
{
  unsigned int i,j;
  
  std::fill(d_symbol_phase_diff.begin(), d_symbol_phase_diff.end(), 0);
  for(i = 0; i < d_fft_length-2; i++) {
    d_symbol_phase_diff[i] = norm(symbol[i] - symbol[i+2]);
  }

  // sweep through all possible/allowed frequency offsets and select the best
  int index = 0;
  float max = 0, sum=0;
  for(i =  zeros_on_left - d_freq_shift_len; i < zeros_on_left + d_freq_shift_len; i++) {
    sum = 0;
    for(j = 0; j < d_occupied_carriers; j++) {
      sum += (d_known_phase_diff[j] * d_symbol_phase_diff[i+j]);
    }
    if(sum > max) {
      max = sum;
      index = i;
    }
  }
  
  // set the coarse frequency offset relative to the edge of the occupied tones
  d_coarse_freq = index - zeros_on_left;
  int d_old = d_coarse_freq;
  d_coarse_freq = 0; //hack apurv++
  if(d_old != 0 || d_coarse_freq != 0) {
     printf("old: %d, new: %d\n", d_old, d_coarse_freq); fflush(stdout);
  }
}

void
digital_ofdm_frame_acquisition::calculate_equalizer(const gr_complex *symbol, int zeros_on_left)
{
  unsigned int i=0;
  const std::vector<gr_complex> &known_symbol = d_known_symbol[d_cur_symbol];
  gr_complex comp = coarse_freq_comp(d_coarse_freq, d_cur_symbol);
  //printf("calculate_equalizer, cur_symbol: %d, comp (%f, %f)\n", d_cur_symbol, comp.real(), comp.imag()); fflush(stdout);
 
  if(d_cur_symbol == 0) {
     // Set first tap of equalizer
     d_hestimate[0] = known_symbol[0] / 
        (coarse_freq_comp(d_coarse_freq,1)*symbol[zeros_on_left+d_coarse_freq]);

    // set every even tap based on known symbol
    // linearly interpolate between set carriers to set zero-filled carriers
    // FIXME: is this the best way to set this?
    for(i = 2; i < d_occupied_carriers; i+=2) {
       d_hestimate[i] = known_symbol[i] / 
         (coarse_freq_comp(d_coarse_freq,1)*(symbol[i+zeros_on_left+d_coarse_freq]));
       d_hestimate[i-1] = (d_hestimate[i] + d_hestimate[i-2]) / gr_complex(2.0, 0.0);    
    }

    // with even number of carriers; last equalizer tap is wrong
    if(!(d_occupied_carriers & 1)) {
       d_hestimate[d_occupied_carriers-1] = d_hestimate[d_occupied_carriers-2];
    }
  }
  else {
    for(i = 0; i < d_occupied_carriers; ++i) {
       gr_complex tx = known_symbol[i];
       gr_complex rx = symbol[i+zeros_on_left+d_coarse_freq];
       d_hestimate[i] = tx/(rx*comp);
    }
  }

  if(VERBOSE) {
    fprintf(stderr, "Equalizer setting:\n");
    for(i = 0; i < d_occupied_carriers; i++) {
      gr_complex sym = coarse_freq_comp(d_coarse_freq,d_cur_symbol)*symbol[i+zeros_on_left+d_coarse_freq];
      gr_complex output = sym * d_hestimate[i];
      fprintf(stderr, "sym: %+.4f + j%+.4f  ks: %+.4f + j%+.4f  eq: %+.4f + j%+.4f  ==>  %+.4f + j%+.4f\n", 
	      sym .real(), sym.imag(),
	      d_known_symbol[d_cur_symbol][i].real(), d_known_symbol[d_cur_symbol][i].imag(),
	      d_hestimate[i].real(), d_hestimate[i].imag(),
	      output.real(), output.imag());
    }
    fprintf(stderr, "\n");
  }
}

int
digital_ofdm_frame_acquisition::general_work(int noutput_items,
					gr_vector_int &ninput_items,
					gr_vector_const_void_star &input_items,
					gr_vector_void_star &output_items)
{
  gr_complex *symbol = (gr_complex *)input_items[0];
  const char *signal_in = (const char *)input_items[1];

  gr_complex *out = (gr_complex *) output_items[0];
  char *signal_out = (char *) output_items[1];
  
  int unoccupied_carriers = d_fft_length - d_occupied_carriers;
  int zeros_on_left = (int)ceil(unoccupied_carriers/2.0);

  /* apurv++: sampler input */
  gr_complex *out3 = NULL;
  gr_complex *in2 = NULL;
  if(output_items.size() >= 4 && input_items.size() >= 3) {
     out3 = (gr_complex *) output_items[3];
     in2 =  (gr_complex *) input_items[2];
     memcpy(out3, in2, sizeof(gr_complex) * d_fft_length); 
  }

  /* apurv++: log hestimates */
  gr_complex *hout = NULL;
  if(output_items.size() >= 3)
      hout = (gr_complex *) output_items[2];
  
  if(signal_in[0]) {
    d_cur_symbol = 0;
    d_phase_count = 1;
    correlate(symbol, zeros_on_left);
    //calculate_equalizer(symbol, zeros_on_left);
    signal_out[0] = 1;
  }
  else {
    signal_out[0] = 0;
  } 

  if(d_cur_symbol < d_known_symbol.size()) {
     calculate_equalizer(symbol, zeros_on_left);
  }
  d_cur_symbol++;
 

#if 1
  if(output_items.size() >= 3) {
     memcpy(hout, d_hestimate, sizeof(gr_complex) * d_occupied_carriers);
     memcpy(out, &symbol[zeros_on_left], sizeof(gr_complex) * d_occupied_carriers);
     assert(d_coarse_freq == 0);
  }
#else  
  for(unsigned int i = 0; i < d_occupied_carriers; i++) {
    if(output_items.size() >= 3) hout[i] = d_hestimate[i];								//apurv++
    out[i] = d_hestimate[i]*coarse_freq_comp(d_coarse_freq, d_phase_count)*symbol[i+zeros_on_left+d_coarse_freq];
  }
#endif

  d_phase_count++;
  if(d_phase_count == MAX_NUM_SYMBOLS) {
    d_phase_count = 1;
  }

  //test_timestamp(1);				// enable to track tag propagation
  consume_each(1);
  return 1;
}

#if 0
/* just for debugging - tracking propagation of tags from sampler to sink */
inline void
digital_ofdm_frame_acquisition::test_timestamp(int output_items) {
  unsigned int tag_port = 1;
  std::vector<gr_tag_t> rx_sync_tags1;
  const uint64_t nread1 = nitems_read(tag_port);
  get_tags_in_range(rx_sync_tags1, tag_port, nread1, nread1+output_items, SYNC_TIME);

  printf("(ACQ) nread1: %llu, output_items: %d\n", nread1, output_items); fflush(stdout);
  if(rx_sync_tags1.size()>0) {
     size_t t = rx_sync_tags1.size()-1;
     uint64_t offset = rx_sync_tags1[t].offset;

     const pmt::pmt_t &value = rx_sync_tags1[t].value;
     uint64_t sync_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
     double sync_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));
     printf("test_timestamp1 (ACQ):: found %d tags, offset: %llu, output_items: %d, nread1: %llu value[%llu, %f]\n", rx_sync_tags1.size(), rx_sync_tags1[t].offset, output_items, nread1, sync_secs, sync_frac_of_secs); fflush(stdout);
  } else {
     //std::cerr << "ACQ---- Header received, with no sync timestamp1?\n";
  }
}
#endif
