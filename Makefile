CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

.PHONY: test test-dbcell test-new-stages test-karplus-sustain test-sweep test-tilt test-zipper test-stereo test-steal test-staccato test-resonance test-loop test-darken test-fastplay test-timbre test-release test-mixer test-bitcrush test-karplusdecay test-gateremoval test-wobble test-releaseresonance clean

test: test_dsp test-dbcell test-new-stages test-karplus-sustain test-sweep test-tilt test-zipper test-stereo test-steal test-staccato test-resonance test-loop test-darken test-fastplay test-timbre test-release test-mixer test-bitcrush test-karplusdecay test-gateremoval test-wobble
	./test_dsp

test-dbcell: test_dbcell
	./test_dbcell

test-new-stages: test_new_stages
	./test_new_stages

test-karplus-sustain: test_karplus_sustain
	./test_karplus_sustain

test-sweep: test_comprehensive_sweep
	./test_comprehensive_sweep

test-tilt: test_tilt_filter
	./test_tilt_filter

test-zipper: test_zipper
	./test_zipper

test-stereo: test_stereo_pan
	./test_stereo_pan

test-steal: test_voice_steal
	./test_voice_steal

test-staccato: test_staccato_steal
	./test_staccato_steal

test-resonance: test_resonance_evenness
	./test_resonance_evenness

test-loop: test_loop_layer
	./test_loop_layer

test-darken: test_release_darken
	./test_release_darken

test-fastplay: test_fast_play
	./test_fast_play

test-timbre: test_timbre_variety
	./test_timbre_variety

test-release: test_release_curve
	./test_release_curve

test-mixer: test_fixed_mixer
	./test_fixed_mixer

test-bitcrush: test_bitcrush_reintro
	./test_bitcrush_reintro

test-karplusdecay: test_karplus_decay_variety
	./test_karplus_decay_variety

test-gateremoval: test_gate_removal
	./test_gate_removal

test-wobble: test_mellotron_wobble
	./test_mellotron_wobble

test-releaseresonance: test_release_resonance
	./test_release_resonance

test_dsp: src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_dsp.c -o test_dsp $(LDFLAGS)

test_dbcell: src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/dbcell_dsp.c test/test_dbcell.c -o test_dbcell $(LDFLAGS)

test_new_stages: src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_new_stages.c -o test_new_stages $(LDFLAGS)

test_karplus_sustain: src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_sustain.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_sustain.c -o test_karplus_sustain $(LDFLAGS)

test_comprehensive_sweep: src/distroy_dsp.c src/noiseboy_dsp.c test/test_comprehensive_sweep.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_comprehensive_sweep.c -o test_comprehensive_sweep $(LDFLAGS)

test_tilt_filter: src/distroy_dsp.c src/noiseboy_dsp.c test/test_tilt_filter.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_tilt_filter.c -o test_tilt_filter $(LDFLAGS)

test_zipper: src/distroy_dsp.c src/noiseboy_dsp.c test/test_zipper.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_zipper.c -o test_zipper $(LDFLAGS)

test_stereo_pan: src/distroy_dsp.c src/noiseboy_dsp.c test/test_stereo_pan.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_stereo_pan.c -o test_stereo_pan $(LDFLAGS)

test_voice_steal: src/distroy_dsp.c src/noiseboy_dsp.c test/test_voice_steal.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_voice_steal.c -o test_voice_steal $(LDFLAGS)

test_staccato_steal: src/distroy_dsp.c src/noiseboy_dsp.c test/test_staccato_steal.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_staccato_steal.c -o test_staccato_steal $(LDFLAGS)

test_resonance_evenness: src/distroy_dsp.c src/noiseboy_dsp.c test/test_resonance_evenness.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_resonance_evenness.c -o test_resonance_evenness $(LDFLAGS)

test_loop_layer: src/distroy_dsp.c src/noiseboy_dsp.c test/test_loop_layer.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_loop_layer.c -o test_loop_layer $(LDFLAGS)

test_release_darken: src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_darken.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_darken.c -o test_release_darken $(LDFLAGS)

test_fast_play: src/distroy_dsp.c src/noiseboy_dsp.c test/test_fast_play.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_fast_play.c -o test_fast_play $(LDFLAGS)

test_timbre_variety: src/distroy_dsp.c src/noiseboy_dsp.c test/test_timbre_variety.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_timbre_variety.c -o test_timbre_variety $(LDFLAGS)

test_release_curve: src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_curve.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_curve.c -o test_release_curve $(LDFLAGS)

test_fixed_mixer: src/distroy_dsp.c src/noiseboy_dsp.c test/test_fixed_mixer.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_fixed_mixer.c -o test_fixed_mixer $(LDFLAGS)

test_bitcrush_reintro: src/distroy_dsp.c src/noiseboy_dsp.c test/test_bitcrush_reintro.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_bitcrush_reintro.c -o test_bitcrush_reintro $(LDFLAGS)

test_karplus_decay_variety: src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_decay_variety.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_karplus_decay_variety.c -o test_karplus_decay_variety $(LDFLAGS)

test_gate_removal: src/distroy_dsp.c src/dbcell_dsp.c src/noiseboy_dsp.c test/test_gate_removal.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/dbcell_dsp.c src/noiseboy_dsp.c test/test_gate_removal.c -o test_gate_removal $(LDFLAGS)

test_mellotron_wobble: src/distroy_dsp.c src/noiseboy_dsp.c test/test_mellotron_wobble.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_mellotron_wobble.c -o test_mellotron_wobble $(LDFLAGS)

test_release_resonance: src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_resonance.c
	$(CC) $(CFLAGS) -Isrc src/distroy_dsp.c src/noiseboy_dsp.c test/test_release_resonance.c -o test_release_resonance $(LDFLAGS)

clean:
	rm -f test_dsp test_dbcell test_new_stages test_karplus_sustain test_comprehensive_sweep test_tilt_filter test_zipper test_stereo_pan test_voice_steal test_staccato_steal test_resonance_evenness test_loop_layer test_release_darken test_fast_play test_timbre_variety test_release_curve test_fixed_mixer test_bitcrush_reintro test_karplus_decay_variety test_gate_removal test_mellotron_wobble test_release_resonance *.o dsp.so
