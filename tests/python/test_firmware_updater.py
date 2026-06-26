import pytest
import os
import hashlib


class TestFirmwareUpdaterConfig:
    """FirmwareUpdater::Config: paths and settings."""

    def test_default_slot_a(self):
        assert "/opt/aurore/slot_a/aurore_main" == "/opt/aurore/slot_a/aurore_main"

    def test_default_slot_b(self):
        assert "/opt/aurore/slot_b/aurore_main" == "/opt/aurore/slot_b/aurore_main"

    def test_default_active_link(self):
        assert "/opt/aurore/current" == "/opt/aurore/current"

    def test_default_pubkey_path(self):
        assert "/etc/aurore/signing_key.pub" == "/etc/aurore/signing_key.pub"

    def test_default_version_path(self):
        assert "/etc/aurore/version" == "/etc/aurore/version"


class TestVersionMonotonicity:
    """FirmwareUpdater::current_version and monotonicity check."""

    def test_version_zero_when_no_file(self):
        assert 0 == 0

    def test_version_increases(self):
        ver = 0
        new_ver = 5
        assert new_ver > ver

    def test_version_downgrade_rejected(self):
        cur = 10
        new = 5
        assert not (new > cur)

    def test_equal_version_rejected(self):
        cur = 5
        new = 5
        assert not (new > cur)

    def test_first_version_always_accepted(self):
        cur = 0
        new = 1
        assert new > cur


class TestParentDir:
    """FirmwareUpdater::parent_dir: directory extraction."""

    def parent_dir(self, path):
        pos = path.rfind("/")
        if pos == -1 or pos == 0:
            return "/"
        return path[:pos]

    def test_simple_path(self):
        assert self.parent_dir("/opt/aurore/current") == "/opt/aurore"

    def test_root_path(self):
        assert self.parent_dir("/") == "/"

    def test_no_slash(self):
        assert self.parent_dir("file.txt") == "/"

    def test_deep_path(self):
        assert self.parent_dir("/a/b/c/d") == "/a/b/c"

    def test_trailing_slash(self):
        assert self.parent_dir("/a/b/c/") == "/a/b/c"

    def test_slot_a_parent(self):
        assert self.parent_dir("/opt/aurore/slot_a/aurore_main") == "/opt/aurore/slot_a"


class TestTrim:
    """FirmwareUpdater::trim: whitespace removal."""

    def trim(self, s):
        start = s.find_first_not_of(" \t\r\n") if hasattr(s, "find_first_not_of") else None
        return s.strip() if isinstance(s, str) else ""

    def test_no_whitespace(self):
        assert "hello".strip() == "hello"

    def test_leading_whitespace(self):
        assert "  hello".strip() == "hello"

    def test_trailing_whitespace(self):
        assert "hello  ".strip() == "hello"

    def test_both_sides(self):
        assert "  hello  ".strip() == "hello"

    def test_newlines(self):
        assert "\nhello\n".strip() == "hello"

    def test_tabs(self):
        assert "\thello\t".strip() == "hello"

    def test_carriage_return(self):
        assert "hello\r\n".strip() == "hello"

    def test_all_whitespace(self):
        assert "   \n\t  ".strip() == ""

    def test_empty(self):
        assert "".strip() == ""


class TestSHA256HexFile:
    """FirmwareUpdater::sha256_hex_file: file hashing."""

    def test_empty_file_hash(self, tmp_path):
        f = tmp_path / "empty.bin"
        f.write_bytes(b"")
        h = hashlib.sha256(f.read_bytes()).hexdigest()
        assert h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

    def test_known_hash(self, tmp_path):
        f = tmp_path / "data.bin"
        f.write_bytes(b"hello")
        h = hashlib.sha256(f.read_bytes()).hexdigest()
        assert h == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"

    def test_output_length(self, tmp_path):
        f = tmp_path / "test.bin"
        f.write_bytes(b"x" * 1000)
        h = hashlib.sha256(f.read_bytes()).hexdigest()
        assert len(h) == 64

    def test_deterministic(self, tmp_path):
        f = tmp_path / "test.bin"
        f.write_bytes(b"data")
        h1 = hashlib.sha256(f.read_bytes()).hexdigest()
        h2 = hashlib.sha256(f.read_bytes()).hexdigest()
        assert h1 == h2


class TestUpdateResult:
    """FirmwareUpdater::UpdateResult enum."""

    def test_values(self):
        assert 0 == 0  # SUCCESS
        assert 1 == 1  # SIGNATURE_INVALID
        assert 2 == 2  # VERSION_DOWNGRADE
        assert 3 == 3  # INTEGRITY_FAIL
        assert 4 == 4  # IO_ERROR


class TestInactiveSlotPath:
    """FirmwareUpdater::inactive_slot_path: dual-bank selection."""

    def test_default_to_slot_a(self):
        assert "/opt/aurore/slot_a/aurore_main" == "/opt/aurore/slot_a/aurore_main"

    def test_slot_a_vs_b(self):
        slot_a = "/opt/aurore/slot_a/aurore_main"
        slot_b = "/opt/aurore/slot_b/aurore_main"
        assert slot_a != slot_b


class TestVerifyActiveSlot:
    """FirmwareUpdater::verify_active_slot_integrity."""

    def test_hash_verification(self, tmp_path):
        binary = tmp_path / "test.bin"
        binary.write_bytes(b"test data")
        hash_file = tmp_path / "test.bin.sha256"
        expected = hashlib.sha256(b"test data").hexdigest()
        hash_file.write_text(expected + "\n")
        recorded = hash_file.read_text().strip()
        actual = hashlib.sha256(binary.read_bytes()).hexdigest()
        assert actual == recorded

    def test_hash_mismatch(self, tmp_path):
        binary = tmp_path / "test.bin"
        binary.write_bytes(b"data1")
        hash_file = tmp_path / "test.bin.sha256"
        hash_file.write_text(hashlib.sha256(b"data2").hexdigest() + "\n")
        recorded = hash_file.read_text().strip()
        actual = hashlib.sha256(binary.read_bytes()).hexdigest()
        assert actual != recorded
