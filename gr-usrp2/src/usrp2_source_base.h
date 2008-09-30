/* -*- c++ -*- */
/*
 * Copyright 2008 Free Software Foundation, Inc.
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

#ifndef INCLUDED_USRP2_SOURCE_BASE_H
#define INCLUDED_USRP2_SOURCE_BASE_H

#include <usrp2_base.h>

/*!
 * Base class for all USRP2 source blocks
 */
class usrp2_source_base : public usrp2_base 
{
protected:
  usrp2_source_base(const char *name,
		    gr_io_signature_sptr output_signature,
		    const std::string &ifc,
		    const std::string &mac)
    throw (std::runtime_error);

public:
  ~usrp2_source_base();

  /*!
   * \brief Set receiver gain
   */
  bool set_gain(double gain);

  /*!
   * \brief Set receiver center frequency
   */
  bool set_center_freq(double frequency, usrp2::tune_result *tr);
   
  /*!
   * \brief Set receive decimation rate
   */
  bool set_decim(int decimation_factor);

  /*!
   * \brief Called by scheduler when starting flowgraph
   */
  virtual bool start();
  
  /*!
   * \brief Called by scheduler when stopping flowgraph
   */
  virtual bool stop();
};

#endif /* INCLUDED_USRP2_SOURCE_BASE_H */