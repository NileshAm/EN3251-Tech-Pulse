import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from app import Store, validate, summarize

def sample(seq=0,**kw):
    return dict(device_id='test',boot_id='boot',seq=seq,voltage_v=230,current_a=1,
                power_w=1000,power_factor=1,frequency_hz=50,simulated=False,**kw)

class TelemetryTests(unittest.TestCase):
    def test_validation(self):
        for key,value in [('power_w',float('nan')),('seq',True),('device_id','bad/topic'),('simulated','false')]:
            d=sample();d[key]=value
            with self.assertRaises(ValueError):validate(d)
    def test_deduplication(self):
        with tempfile.TemporaryDirectory() as directory:
            store=Store(Path(directory)/'test.sqlite3')
            self.assertTrue(store.insert(sample()))
            self.assertFalse(store.insert(sample()))
            self.assertEqual(len(store.devices()),1)
    def test_energy_excludes_gaps_and_reboots(self):
        rows=[dict(sample(i),ts=t) for i,t in enumerate([0,2,4,100])]
        rows.append(dict(sample(4),ts=102,boot_id='new'))
        stats=summarize(rows)
        self.assertAlmostEqual(stats['energy_kwh'],1000*4/3600000)
        self.assertEqual(stats['covered_seconds'],4)
    def test_export_does_not_cancel_import(self):
        rows=[dict(sample(i),ts=i*2,power_w=-1000) for i in range(2)]
        self.assertEqual(summarize(rows)['energy_kwh'],0)

if __name__=='__main__':unittest.main()
