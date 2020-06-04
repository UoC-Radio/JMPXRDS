#!/usr/bin/env python2
# -*- coding: utf-8 -*-
##################################################
# GNU Radio Python Flow Graph
# Title: JMPXRDS Receiver (based on gr_rds)
# Author: Nick Kossifidis
# Description: This flow can be used for debugging JMPXRDS
# GNU Radio version: 3.7.13.5
##################################################
import threading


if __name__ == '__main__':
    import ctypes
    import sys
    if sys.platform.startswith('linux'):
        try:
            x11 = ctypes.cdll.LoadLibrary('libX11.so')
            x11.XInitThreads()
        except:
            print "Warning: failed to XInitThreads()"

from gnuradio import analog
from gnuradio import audio
from gnuradio import blocks
from gnuradio import digital
from gnuradio import digital;import cmath
from gnuradio import eng_notation
from gnuradio import filter
from gnuradio import gr
from gnuradio import wxgui
from gnuradio.eng_option import eng_option
from gnuradio.fft import window
from gnuradio.filter import firdes
from gnuradio.wxgui import fftsink2
from gnuradio.wxgui import forms
from gnuradio.wxgui import scopesink2
from gnuradio.wxgui import waterfallsink2
from grc_gnuradio import wxgui as grc_wxgui
from optparse import OptionParser
import math
import pmt
import rds
import wx


class jmpxrds_rx(grc_wxgui.top_block_gui):

    def __init__(self):
        grc_wxgui.top_block_gui.__init__(self, title="JMPXRDS Receiver (based on gr_rds)")
        _icon_path = "/usr/share/icons/hicolor/32x32/apps/gnuradio-grc.png"
        self.SetIcon(wx.Icon(_icon_path, wx.BITMAP_TYPE_ANY))

        self._lock = threading.RLock()

        ##################################################
        # Variables
        ##################################################
        self.volume = volume = -3
        self.mpx_samp_rate_0 = mpx_samp_rate_0 = 192000
        self.mpx_samp_rate = mpx_samp_rate = 192000
        self.audio_samp_rate = audio_samp_rate = 96000

        ##################################################
        # Blocks
        ##################################################
        _volume_sizer = wx.BoxSizer(wx.VERTICAL)
        self._volume_text_box = forms.text_box(
        	parent=self.GetWin(),
        	sizer=_volume_sizer,
        	value=self.volume,
        	callback=self.set_volume,
        	label='Volume',
        	converter=forms.float_converter(),
        	proportion=0,
        )
        self._volume_slider = forms.slider(
        	parent=self.GetWin(),
        	sizer=_volume_sizer,
        	value=self.volume,
        	callback=self.set_volume,
        	minimum=-20,
        	maximum=10,
        	num_steps=300,
        	style=wx.SL_HORIZONTAL,
        	cast=float,
        	proportion=1,
        )
        self.GridAdd(_volume_sizer, 0, 0, 1, 1)
        self.nb = self.nb = wx.Notebook(self.GetWin(), style=wx.NB_TOP)
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "MPX Plot")
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "MPX Waterfall")
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "L+R")
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "L-R")
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "RDS Plot")
        self.nb.AddPage(grc_wxgui.Panel(self.nb), "RDS Constelation")
        self.GridAdd(self.nb, 1, 0, 1, 1)
        self.wxgui_waterfallsink2_0 = waterfallsink2.waterfall_sink_f(
        	self.nb.GetPage(1).GetWin(),
        	baseband_freq=0,
        	dynamic_range=100,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate,
        	fft_size=512,
        	fft_rate=15,
        	average=False,
        	avg_alpha=None,
        	title='MPX Waterfall Plot',
        )
        self.nb.GetPage(1).Add(self.wxgui_waterfallsink2_0.win)
        self.wxgui_scopesink2_1 = scopesink2.scope_sink_c(
        	self.nb.GetPage(5).GetWin(),
        	title='Scope Plot',
        	sample_rate=mpx_samp_rate / 4,
        	v_scale=0.4,
        	v_offset=0,
        	t_scale=0,
        	ac_couple=False,
        	xy_mode=True,
        	num_inputs=1,
        	trig_mode=wxgui.TRIG_MODE_AUTO,
        	y_axis_label='Counts',
        )
        self.nb.GetPage(5).Add(self.wxgui_scopesink2_1.win)
        self.wxgui_fftsink2_0_0_1 = fftsink2.fft_sink_c(
        	self.nb.GetPage(0).GetWin(),
        	baseband_freq=0,
        	y_per_div=10,
        	y_divs=10,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate,
        	fft_size=1024,
        	fft_rate=15,
        	average=True,
        	avg_alpha=0.8,
        	title='MPX FFT',
        	peak_hold=False,
        	win=window.flattop,
        )
        self.nb.GetPage(0).Add(self.wxgui_fftsink2_0_0_1.win)
        self.wxgui_fftsink2_0_0_0_1_0_1 = fftsink2.fft_sink_c(
        	self.nb.GetPage(4).GetWin(),
        	baseband_freq=0,
        	y_per_div=10,
        	y_divs=10,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate/4,
        	fft_size=1024,
        	fft_rate=12,
        	average=False,
        	avg_alpha=None,
        	title='RDS',
        	peak_hold=False,
        	win=window.blackmanharris,
        )
        self.nb.GetPage(4).Add(self.wxgui_fftsink2_0_0_0_1_0_1.win)
        self.wxgui_fftsink2_0_0_0_1 = fftsink2.fft_sink_f(
        	self.nb.GetPage(3).GetWin(),
        	baseband_freq=0,
        	y_per_div=10,
        	y_divs=10,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate,
        	fft_size=1024,
        	fft_rate=15,
        	average=False,
        	avg_alpha=None,
        	title='L-R',
        	peak_hold=False,
        	win=window.flattop,
        )
        self.nb.GetPage(3).Add(self.wxgui_fftsink2_0_0_0_1.win)
        self.wxgui_fftsink2_0_0_0 = fftsink2.fft_sink_f(
        	self.nb.GetPage(2).GetWin(),
        	baseband_freq=0,
        	y_per_div=10,
        	y_divs=10,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate,
        	fft_size=1024,
        	fft_rate=15,
        	average=False,
        	avg_alpha=None,
        	title='L+R',
        	peak_hold=False,
        	win=window.flattop,
        )
        self.nb.GetPage(2).Add(self.wxgui_fftsink2_0_0_0.win)
        self.wxgui_fftsink2_0_0 = fftsink2.fft_sink_f(
        	self.nb.GetPage(0).GetWin(),
        	baseband_freq=0,
        	y_per_div=10,
        	y_divs=10,
        	ref_level=0,
        	ref_scale=2.0,
        	sample_rate=mpx_samp_rate,
        	fft_size=1024,
        	fft_rate=15,
        	average=True,
        	avg_alpha=0.8,
        	title='MPX FFT',
        	peak_hold=False,
        	win=window.flattop,
        )
        self.nb.GetPage(0).Add(self.wxgui_fftsink2_0_0.win)
        self.root_raised_cosine_filter_0 = filter.fir_filter_ccf(1, firdes.root_raised_cosine(
        	1, mpx_samp_rate/4, 2375, 1, 100))
        self.rational_resampler_xxx_0_0 = filter.rational_resampler_fff(
                interpolation=audio_samp_rate,
                decimation=mpx_samp_rate,
                taps=None,
                fractional_bw=0.4,
        )
        self.rational_resampler_xxx_0 = filter.rational_resampler_fff(
                interpolation=audio_samp_rate,
                decimation=mpx_samp_rate,
                taps=None,
                fractional_bw=0.4,
        )
        self.gr_rds_parser_0 = rds.parser(False, False, 0)
        self.gr_rds_panel_0 = rds.rdsPanel(96700000, self.GetWin())
        self.Add(self.gr_rds_panel_0.panel)
        self.gr_rds_decoder_0 = rds.decoder(False, False)
        self.freq_xlating_fir_filter_xxx_2 = filter.freq_xlating_fir_filter_fcf(1, (firdes.low_pass(1.0,mpx_samp_rate,17e3,3e3,firdes.WIN_HAMMING)), 38000, mpx_samp_rate)
        self.freq_xlating_fir_filter_xxx_1 = filter.freq_xlating_fir_filter_fcc(4, (firdes.low_pass(1.0,mpx_samp_rate,2.4e3,2e3,firdes.WIN_HAMMING)), 57e3, mpx_samp_rate)
        self.fir_filter_xxx_1 = filter.fir_filter_fff(1, (firdes.low_pass(1.0,mpx_samp_rate,17e3,3e3,firdes.WIN_HAMMING)))
        self.fir_filter_xxx_1.declare_sample_delay(0)
        self.digital_mpsk_receiver_cc_0 = digital.mpsk_receiver_cc(2, 0, 1*cmath.pi/100.0, -0.06, 0.06, 0.5, 0.05, mpx_samp_rate/4/2375.0, 0.001, 0.005)
        self.digital_diff_decoder_bb_0 = digital.diff_decoder_bb(2)
        self.digital_binary_slicer_fb_0 = digital.binary_slicer_fb()
        self.blocks_sub_xx_0 = blocks.sub_ff(1)
        self.blocks_multiply_const_vxx_0_0 = blocks.multiply_const_vff((10**(1.*(volume)/10), ))
        self.blocks_multiply_const_vxx_0 = blocks.multiply_const_vff((10**(1.*(volume)/10), ))
        self.blocks_keep_one_in_n_0 = blocks.keep_one_in_n(gr.sizeof_char*1, 2)
        self.blocks_file_source_0 = blocks.file_source(gr.sizeof_float*1, '/run/user/1000/jmpxrds.sock', False)
        self.blocks_file_source_0.set_begin_tag(pmt.PMT_NIL)
        self.blocks_complex_to_real_0_0 = blocks.complex_to_real(1)
        self.blocks_complex_to_real_0 = blocks.complex_to_real(1)
        self.blocks_add_xx_0 = blocks.add_vff(1)
        self.audio_sink_0 = audio.sink(audio_samp_rate, '', True)
        self.analog_frequency_modulator_fc_0 = analog.frequency_modulator_fc(1)
        self.analog_fm_deemph_0_0_0 = analog.fm_deemph(fs=mpx_samp_rate, tau=50e-6)
        self.analog_fm_deemph_0_0 = analog.fm_deemph(fs=mpx_samp_rate, tau=50e-6)



        ##################################################
        # Connections
        ##################################################
        self.msg_connect((self.gr_rds_decoder_0, 'out'), (self.gr_rds_parser_0, 'in'))
        self.msg_connect((self.gr_rds_parser_0, 'out'), (self.gr_rds_panel_0, 'in'))
        self.connect((self.analog_fm_deemph_0_0, 0), (self.blocks_multiply_const_vxx_0_0, 0))
        self.connect((self.analog_fm_deemph_0_0_0, 0), (self.blocks_multiply_const_vxx_0, 0))
        self.connect((self.analog_frequency_modulator_fc_0, 0), (self.wxgui_fftsink2_0_0_1, 0))
        self.connect((self.blocks_add_xx_0, 0), (self.analog_fm_deemph_0_0_0, 0))
        self.connect((self.blocks_complex_to_real_0, 0), (self.blocks_add_xx_0, 1))
        self.connect((self.blocks_complex_to_real_0, 0), (self.blocks_sub_xx_0, 1))
        self.connect((self.blocks_complex_to_real_0, 0), (self.wxgui_fftsink2_0_0_0_1, 0))
        self.connect((self.blocks_complex_to_real_0_0, 0), (self.digital_binary_slicer_fb_0, 0))
        self.connect((self.blocks_file_source_0, 0), (self.analog_frequency_modulator_fc_0, 0))
        self.connect((self.blocks_file_source_0, 0), (self.fir_filter_xxx_1, 0))
        self.connect((self.blocks_file_source_0, 0), (self.freq_xlating_fir_filter_xxx_1, 0))
        self.connect((self.blocks_file_source_0, 0), (self.freq_xlating_fir_filter_xxx_2, 0))
        self.connect((self.blocks_file_source_0, 0), (self.wxgui_fftsink2_0_0, 0))
        self.connect((self.blocks_file_source_0, 0), (self.wxgui_waterfallsink2_0, 0))
        self.connect((self.blocks_keep_one_in_n_0, 0), (self.digital_diff_decoder_bb_0, 0))
        self.connect((self.blocks_multiply_const_vxx_0, 0), (self.rational_resampler_xxx_0, 0))
        self.connect((self.blocks_multiply_const_vxx_0_0, 0), (self.rational_resampler_xxx_0_0, 0))
        self.connect((self.blocks_sub_xx_0, 0), (self.analog_fm_deemph_0_0, 0))
        self.connect((self.digital_binary_slicer_fb_0, 0), (self.blocks_keep_one_in_n_0, 0))
        self.connect((self.digital_diff_decoder_bb_0, 0), (self.gr_rds_decoder_0, 0))
        self.connect((self.digital_mpsk_receiver_cc_0, 0), (self.blocks_complex_to_real_0_0, 0))
        self.connect((self.digital_mpsk_receiver_cc_0, 0), (self.wxgui_scopesink2_1, 0))
        self.connect((self.fir_filter_xxx_1, 0), (self.blocks_add_xx_0, 0))
        self.connect((self.fir_filter_xxx_1, 0), (self.blocks_sub_xx_0, 0))
        self.connect((self.fir_filter_xxx_1, 0), (self.wxgui_fftsink2_0_0_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_1, 0), (self.root_raised_cosine_filter_0, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_1, 0), (self.wxgui_fftsink2_0_0_0_1_0_1, 0))
        self.connect((self.freq_xlating_fir_filter_xxx_2, 0), (self.blocks_complex_to_real_0, 0))
        self.connect((self.rational_resampler_xxx_0, 0), (self.audio_sink_0, 0))
        self.connect((self.rational_resampler_xxx_0_0, 0), (self.audio_sink_0, 1))
        self.connect((self.root_raised_cosine_filter_0, 0), (self.digital_mpsk_receiver_cc_0, 0))

    def get_volume(self):
        return self.volume

    def set_volume(self, volume):
        with self._lock:
            self.volume = volume
            self._volume_slider.set_value(self.volume)
            self._volume_text_box.set_value(self.volume)
            self.blocks_multiply_const_vxx_0_0.set_k((10**(1.*(self.volume)/10), ))
            self.blocks_multiply_const_vxx_0.set_k((10**(1.*(self.volume)/10), ))

    def get_mpx_samp_rate_0(self):
        return self.mpx_samp_rate_0

    def set_mpx_samp_rate_0(self, mpx_samp_rate_0):
        with self._lock:
            self.mpx_samp_rate_0 = mpx_samp_rate_0

    def get_mpx_samp_rate(self):
        return self.mpx_samp_rate

    def set_mpx_samp_rate(self, mpx_samp_rate):
        with self._lock:
            self.mpx_samp_rate = mpx_samp_rate
            self.wxgui_waterfallsink2_0.set_sample_rate(self.mpx_samp_rate)
            self.wxgui_scopesink2_1.set_sample_rate(self.mpx_samp_rate / 4)
            self.wxgui_fftsink2_0_0_1.set_sample_rate(self.mpx_samp_rate)
            self.wxgui_fftsink2_0_0_0_1_0_1.set_sample_rate(self.mpx_samp_rate/4)
            self.wxgui_fftsink2_0_0_0_1.set_sample_rate(self.mpx_samp_rate)
            self.wxgui_fftsink2_0_0_0.set_sample_rate(self.mpx_samp_rate)
            self.wxgui_fftsink2_0_0.set_sample_rate(self.mpx_samp_rate)
            self.root_raised_cosine_filter_0.set_taps(firdes.root_raised_cosine(1, self.mpx_samp_rate/4, 2375, 1, 100))
            self.freq_xlating_fir_filter_xxx_2.set_taps((firdes.low_pass(1.0,self.mpx_samp_rate,17e3,3e3,firdes.WIN_HAMMING)))
            self.freq_xlating_fir_filter_xxx_1.set_taps((firdes.low_pass(1.0,self.mpx_samp_rate,2.4e3,2e3,firdes.WIN_HAMMING)))
            self.fir_filter_xxx_1.set_taps((firdes.low_pass(1.0,self.mpx_samp_rate,17e3,3e3,firdes.WIN_HAMMING)))
            self.digital_mpsk_receiver_cc_0.set_omega(self.mpx_samp_rate/4/2375.0)

    def get_audio_samp_rate(self):
        return self.audio_samp_rate

    def set_audio_samp_rate(self, audio_samp_rate):
        with self._lock:
            self.audio_samp_rate = audio_samp_rate


def main(top_block_cls=jmpxrds_rx, options=None):
    if gr.enable_realtime_scheduling() != gr.RT_OK:
        print "Error: failed to enable real-time scheduling."

    tb = top_block_cls()
    tb.Start(True)
    tb.Wait()


if __name__ == '__main__':
    main()
