"""Release publication boundaries in disposable OS-temp directories only."""
from pathlib import Path
import hashlib
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import release_layout as layout


class ReleaseOutputs(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='drifter-release-output-')
        self.root=Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_new_archive_and_checksum_are_published(self):
        archive=layout.write_zip(self.root/'new.zip','Fixture',{'file.txt':b'fixture'})
        self.assertEqual(Path(str(archive)+'.sha256').read_text(),hashlib.sha256(archive.read_bytes()).hexdigest()+'  new.zip\n')
        self.assertEqual(sorted(p.name for p in self.root.iterdir()),['new.zip','new.zip.sha256'])

    def test_existing_archive_or_sidecar_preserves_both(self):
        for name in ('old.zip','old.zip.sha256'):
            with self.subTest(name=name):
                target=self.root/name;target.write_bytes(b'published')
                with self.assertRaises(RuntimeError):layout.write_zip(self.root/'old.zip','Fixture',{'file':b'changed'})
                self.assertEqual(target.read_bytes(),b'published')
                self.assertEqual(len(list(self.root.iterdir())),1)
                target.unlink()

    def test_raced_output_is_preserved(self):
        target=self.root/'race.zip';original_link=os.link
        def raced(source,destination,*args,**kwargs):
            Path(destination).write_bytes(b'appeared during publication')
            return original_link(source,destination,*args,**kwargs)
        with patch.object(layout.os,'link',side_effect=raced),self.assertRaises(FileExistsError):
            layout.write_zip(target,'Fixture',{'file':b'changed'})
        self.assertEqual(target.read_bytes(),b'appeared during publication')
        self.assertEqual(list(self.root.iterdir()),[target])

    def test_hardlink_and_redirected_existing_output_are_preserved(self):
        outside=self.root/'original';outside.write_bytes(b'original')
        linked=self.root/'alias.zip';os.link(outside,linked)
        with self.assertRaises(RuntimeError):layout.write_zip(linked,'Fixture',{'file':b'changed'})
        self.assertEqual(outside.read_bytes(),b'original');linked.unlink()
        try:linked.symlink_to(outside)
        except OSError:self.skipTest('This host cannot create symlink fixtures.')
        with self.assertRaises(RuntimeError):layout.write_zip(linked,'Fixture',{'file':b'changed'})
        self.assertEqual(outside.read_bytes(),b'original')


if __name__=='__main__':unittest.main()
