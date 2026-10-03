#!/usr/bin/env python3
"""Add the UI test action that CMake's generated Xcode scheme omits."""

import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET


def target(pbxproj: str, name: str, product: str, product_type: str) -> tuple[str, str, str]:
    pattern = rf"(?m)^\s*([A-Fa-f0-9]{{24}}) /\* {re.escape(name)} \*/ = \{{\s*isa = PBXNativeTarget;"
    matches = list(re.finditer(pattern, pbxproj))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Xcode native target named {name}, found {len(matches)}")
    body = pbxproj[matches[0].end():].split("};", 1)[0]
    product_reference = re.search(r"\bproductReference\s*=\s*([A-Fa-f0-9]{24})\b", body)
    if not product_reference:
        raise RuntimeError(f"Xcode target {name} has no productReference")
    type_match = re.search(r'\bproductType\s*=\s*"?([^";]+)"?\s*;', body)
    actual_type = type_match.group(1) if type_match else "missing"
    if actual_type != product_type:
        raise RuntimeError(f"Xcode target {name} has product type {actual_type}, expected {product_type}")
    # CMake may describe a bundle product by its bundle path or its nested
    # executable path. Accept only these exact forms for the validated type.
    file_pattern = (
        rf"(?m)^\s*{product_reference.group(1)} /\* [^*]+ \*/ = "
        rf"\{{\s*isa = PBXFileReference;"
    )
    file_matches = list(re.finditer(file_pattern, pbxproj))
    if len(file_matches) != 1:
        raise RuntimeError(f"Xcode target {name} has no unique product file reference")
    file_body = pbxproj[file_matches[0].end():].split("};", 1)[0]
    path_match = re.search(r'\bpath\s*=\s*(?:"([^"]+)"|([^;]+))\s*;', file_body)
    actual = (path_match.group(1) or path_match.group(2)).strip() if path_match else "missing"
    expected_paths = {name, product}
    if product_type == "com.apple.product-type.bundle.ui-testing":
        expected_paths.add(f"{product}/{name}")
    if actual not in expected_paths:
        raise RuntimeError(f"Xcode target {name} has unexpected product path {actual}")
    return name, product, matches[0].group(1)


def reference(parent: ET.Element, target: tuple[str, str, str], project_name: str) -> None:
    name, product, identifier = target
    ET.SubElement(
        parent,
        "BuildableReference",
        BuildableIdentifier="primary",
        BlueprintIdentifier=identifier,
        BuildableName=product,
        BlueprintName=name,
        ReferencedContainer=f"container:{project_name}",
    )


def write_scheme(project: Path) -> Path:
    pbxproj = (project / "project.pbxproj").read_text(encoding="utf-8")
    app = target(pbxproj, "CPPGameEngineMobile", "CPPGameEngineMobile.app",
                 "com.apple.product-type.application")
    tests = target(pbxproj, "CPPGameEngineMobileUITests", "CPPGameEngineMobileUITests.xctest",
                   "com.apple.product-type.bundle.ui-testing")

    scheme = ET.Element("Scheme", LastUpgradeVersion="2630", version="1.3")
    build = ET.SubElement(scheme, "BuildAction", parallelizeBuildables="YES", buildImplicitDependencies="YES")
    entries = ET.SubElement(build, "BuildActionEntries")
    for build_target in (app, tests):
        entry = ET.SubElement(
            entries,
            "BuildActionEntry",
            buildForTesting="YES",
            buildForRunning="YES" if build_target == app else "NO",
            buildForProfiling="NO",
            buildForArchiving="NO",
            buildForAnalyzing="NO",
        )
        reference(entry, build_target, project.name)

    test_action = ET.SubElement(
        scheme,
        "TestAction",
        buildConfiguration="Debug",
        selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB",
        selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.PosixSpawn",
        shouldUseLaunchSchemeArgsEnv="YES",
    )
    testables = ET.SubElement(test_action, "Testables")
    testable = ET.SubElement(testables, "TestableReference", skipped="NO")
    reference(testable, tests, project.name)
    reference(ET.SubElement(test_action, "MacroExpansion"), app, project.name)

    launch = ET.SubElement(
        scheme,
        "LaunchAction",
        buildConfiguration="Debug",
        selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB",
        selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.PosixSpawn",
        launchStyle="0",
        useCustomWorkingDirectory="NO",
        ignoresPersistentStateOnLaunch="NO",
        debugDocumentVersioning="YES",
        debugServiceExtension="internal",
        allowLocationSimulation="YES",
    )
    reference(ET.SubElement(launch, "BuildableProductRunnable", runnableDebuggingMode="0"), app, project.name)

    schemes = project / "xcshareddata" / "xcschemes"
    schemes.mkdir(parents=True, exist_ok=True)
    output = schemes / "CPPGameEngineMobileUITests.xcscheme"
    ET.indent(scheme)
    ET.ElementTree(scheme).write(output, encoding="utf-8", xml_declaration=True)
    return output


if __name__ == "__main__":
    output = write_scheme(Path(sys.argv[1]))
    print(f"Configured Xcode UI test action: {output}")
