#!/usr/bin/env python3
"""Exercise Xcode product validation against small project fixtures."""

import importlib.util
import tempfile
from pathlib import Path
from xml.etree import ElementTree as ET


SCRIPT = Path(__file__).with_name("write-test-scheme.py")
SPEC = importlib.util.spec_from_file_location("write_test_scheme", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

APP_NAME = "CPPGameEngineMobile"
APP_PRODUCT = f"{APP_NAME}.app"
APP_TYPE = "com.apple.product-type.application"
TEST_NAME = "CPPGameEngineMobileUITests"
TEST_PRODUCT = f"{TEST_NAME}.xctest"
TEST_TYPE = "com.apple.product-type.bundle.ui-testing"


def project_text(app_path: str, test_path: str, test_type: str = TEST_TYPE) -> str:
    return f'''\
AAAAAAAAAAAAAAAAAAAAAAAA /* {APP_NAME} */ = {{
    isa = PBXNativeTarget;
    productReference = CCCCCCCCCCCCCCCCCCCCCCCC;
    productType = {APP_TYPE};
}};
BBBBBBBBBBBBBBBBBBBBBBBB /* {TEST_NAME} */ = {{
    isa = PBXNativeTarget;
    productReference = DDDDDDDDDDDDDDDDDDDDDDDD;
    productType = {test_type};
}};
CCCCCCCCCCCCCCCCCCCCCCCC /* {APP_PRODUCT} */ = {{
    isa = PBXFileReference;
    path = {app_path};
}};
DDDDDDDDDDDDDDDDDDDDDDDD /* {TEST_PRODUCT} */ = {{
    isa = PBXFileReference;
    path = {test_path};
}};
'''


def run_fixture(app_path: str = APP_PRODUCT, test_path: str = TEST_PRODUCT,
                test_type: str = TEST_TYPE) -> Path:
    with tempfile.TemporaryDirectory() as temporary:
        project = Path(temporary) / "CPPGameEngineMobile.xcodeproj"
        project.mkdir()
        (project / "project.pbxproj").write_text(
            project_text(app_path, test_path, test_type), encoding="utf-8")
        output = MODULE.write_scheme(project)
        tree = ET.parse(output)
        assert tree.find(".//TestableReference/BuildableReference[@BuildableName='CPPGameEngineMobileUITests.xctest']") is not None
        return output


def require_rejected(test_path: str, test_type: str = TEST_TYPE) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        project = Path(temporary) / "CPPGameEngineMobile.xcodeproj"
        project.mkdir()
        (project / "project.pbxproj").write_text(
            project_text(APP_PRODUCT, test_path, test_type), encoding="utf-8")
        try:
            MODULE.write_scheme(project)
        except RuntimeError:
            return
        raise AssertionError(f"accepted invalid test product {test_path!r} with type {test_type!r}")


def main() -> None:
    for app_path in (APP_NAME, APP_PRODUCT):
        for test_path in (TEST_NAME, TEST_PRODUCT, f"{TEST_PRODUCT}/{TEST_NAME}"):
            run_fixture(app_path, test_path)
    require_rejected("Other.xctest/CPPGameEngineMobileUITests")
    require_rejected(f"{TEST_PRODUCT}/{TEST_NAME}", "com.apple.product-type.bundle")
    print("Xcode UI test scheme product fixtures passed")


if __name__ == "__main__":
    main()
