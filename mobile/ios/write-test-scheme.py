#!/usr/bin/env python3
"""Add the UI test action that CMake's generated Xcode scheme omits."""

import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET


project = Path(sys.argv[1])
pbxproj = (project / "project.pbxproj").read_text(encoding="utf-8")


def target(name: str, product: str) -> tuple[str, str, str]:
    pattern = rf"(?m)^\s*([A-Fa-f0-9]{{24}}) /\* {re.escape(name)} \*/ = \{{\s*isa = PBXNativeTarget;"
    matches = list(re.finditer(pattern, pbxproj))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one Xcode native target named {name}, found {len(matches)}")
    body = pbxproj[matches[0].end():].split("};", 1)[0]
    product_reference = re.search(
        r"\bproductReference\s*=\s*[A-Fa-f0-9]{24}\s*/\*\s*([^*]+?)\s*\*/\s*;",
        body,
    )
    if not product_reference or product_reference.group(1).strip() != product:
        actual = product_reference.group(1).strip() if product_reference else "missing"
        raise RuntimeError(f"Xcode target {name} has product {actual}, expected {product}")
    return name, product, matches[0].group(1)


app = target("CPPGameEngineMobile", "CPPGameEngineMobile.app")
tests = target("CPPGameEngineMobileUITests", "CPPGameEngineMobileUITests.xctest")


def reference(parent: ET.Element, target: tuple[str, str, str]) -> None:
    name, product, identifier = target
    ET.SubElement(
        parent,
        "BuildableReference",
        BuildableIdentifier="primary",
        BlueprintIdentifier=identifier,
        BuildableName=product,
        BlueprintName=name,
        ReferencedContainer=f"container:{project.name}",
    )


scheme = ET.Element("Scheme", LastUpgradeVersion="2630", version="1.3")
build = ET.SubElement(scheme, "BuildAction", parallelizeBuildables="YES", buildImplicitDependencies="YES")
entries = ET.SubElement(build, "BuildActionEntries")
for target in (app, tests):
    entry = ET.SubElement(
        entries,
        "BuildActionEntry",
        buildForTesting="YES",
        buildForRunning="YES" if target == app else "NO",
        buildForProfiling="NO",
        buildForArchiving="NO",
        buildForAnalyzing="NO",
    )
    reference(entry, target)

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
reference(testable, tests)
reference(ET.SubElement(test_action, "MacroExpansion"), app)

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
reference(ET.SubElement(launch, "BuildableProductRunnable", runnableDebuggingMode="0"), app)

schemes = project / "xcshareddata" / "xcschemes"
schemes.mkdir(parents=True, exist_ok=True)
output = schemes / "CPPGameEngineMobileUITests.xcscheme"
ET.indent(scheme)
ET.ElementTree(scheme).write(output, encoding="utf-8", xml_declaration=True)
print(f"Configured Xcode UI test action: {output}")
