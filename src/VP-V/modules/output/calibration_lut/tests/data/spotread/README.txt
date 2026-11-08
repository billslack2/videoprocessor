Spotread transcripts for CalibrationMeterTests (automatic calibration).

Scripts for fake_spotread.exe (src/VideoProcessor-FakeSpotread). They are
modelled on ArgyllCMS 3.x spotread output for each case VP must handle: a
colorimeter session, a spectro's sensor-calibration prompt, a failed reading,
no instrument, an initialisation failure, a hung instrument (timeout), an
unexpected exit and a pseudo-console redraw. The gated hardware test
(VP_TEST_METER=1) saves the real meter's transcript to
%TEMP%\vp-spotread-transcript.txt; commit such recordings here as
recorded_<meter>.txt and add them to the parser tests.

Recording (calibration phase 2): with [calibration] record_transcripts on,
VP saves every real spotread session in this exact script format
(TranscriptRecorder.h) as recorded_<meter>_<yyyyMMdd-HHmmss>.txt in
luts\calibration\transcripts (or transcript_folder): OUT lines, PROMPT
before each key, KEY for each key VP sent, SLEEP for the meter's time per
reading, EXIT with spotread's exit code, and the meter, the ArgyllCMS
version and spotread's arguments as # comments. Copy one here as
recorded_<meter>.txt and add it to the parser and meter tests; it then
replaces the modelled transcript for that meter. The gated test
CalibrationTranscriptHardwareTests::RecordRealSpotreadTranscript records
one on a real meter (VP_TEST_METER=1).
