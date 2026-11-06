"""Creates a placeholder body for a complete EC11 rotary encoder.

Includes: square body, threaded bushing, shaft, and pin block underneath.
Sized for the bare EC11 (knurled / half-knurled shaft variant).
"""

import traceback
import math
import adsk.core
import adsk.fusion

# Initialize the global variables for the Application and UserInterface objects.
app = adsk.core.Application.get()
ui = app.userInterface


def run(_context: str):
    """This function is called by Fusion when the script is run."""

    try:
        design = app.activeProduct
        root_comp = design.rootComponent

        # Fusion API uses centimeters internally, so all mm values are divided by 10.
        # Origin = center of encoder body, at PCB-mounting surface (top of pins).

        extrudes = root_comp.features.extrudeFeatures

        # --- Block 1: Encoder body (12.5 x 13.5 x 6.5 mm) ---
        # Square-ish metal can that houses the encoder mechanism.
        sketch1 = root_comp.sketches.add(root_comp.xYConstructionPlane)
        sketch1.sketchCurves.sketchLines.addCenterPointRectangle(
            adsk.core.Point3D.create(0, 0, 0),
            adsk.core.Point3D.create(1.25 / 2, 1.35 / 2, 0)
        )
        prof1 = sketch1.profiles.item(0)
        ext1_input = extrudes.createInput(
            prof1, adsk.fusion.FeatureOperations.NewBodyFeatureOperation
        )
        ext1_input.setDistanceExtent(False, adsk.core.ValueInput.createByReal(0.65))
        ext1 = extrudes.add(ext1_input)
        body = ext1.bodies.item(0)

        # --- Block 2: Threaded bushing (Ø7 mm, 7 mm tall) on top center ---
        top_face = ext1.endFaces.item(0)
        sketch2 = root_comp.sketches.add(top_face)
        sketch2.sketchCurves.sketchCircles.addByCenterRadius(
            adsk.core.Point3D.create(0, 0, 0), 0.7 / 2
        )
        prof2 = sketch2.profiles.item(0)
        ext2_input = extrudes.createInput(
            prof2, adsk.fusion.FeatureOperations.JoinFeatureOperation
        )
        ext2_input.setDistanceExtent(False, adsk.core.ValueInput.createByReal(0.7))
        ext2_input.participantBodies = [body]
        extrudes.add(ext2_input)

        # --- Block 3: Shaft (Ø6 mm, 15 mm tall) on top of bushing ---
        # Find the top face of the bushing (the highest circular face).
        bushing_top = None
        max_z = -1.0
        for face in body.faces:
            try:
                # Check if it's a planar face with normal pointing up.
                eval_result = face.evaluator.getNormalAtPoint(face.pointOnFace)
                normal = eval_result[1]
                if normal.z > 0.9:
                    point = face.pointOnFace
                    if point.z > max_z:
                        max_z = point.z
                        bushing_top = face
            except:  # pylint:disable=bare-except
                continue

        if bushing_top is None:
            ui.messageBox('Could not find bushing top face — aborting.')
            return

        sketch3 = root_comp.sketches.add(bushing_top)
        sketch3.sketchCurves.sketchCircles.addByCenterRadius(
            adsk.core.Point3D.create(0, 0, 0), 0.6 / 2
        )
        prof3 = sketch3.profiles.item(0)
        ext3_input = extrudes.createInput(
            prof3, adsk.fusion.FeatureOperations.JoinFeatureOperation
        )
        ext3_input.setDistanceExtent(False, adsk.core.ValueInput.createByReal(1.5))
        ext3_input.participantBodies = [body]
        extrudes.add(ext3_input)

        # --- Block 4: Pin block underneath (12.5 x 13.5 x 5 mm) ---
        # Represents the clearance needed for the 5 pins sticking down into the host PCB.
        # Find the bottom face of the encoder body.
        bottom_face = None
        min_z = 1.0
        for face in body.faces:
            try:
                eval_result = face.evaluator.getNormalAtPoint(face.pointOnFace)
                normal = eval_result[1]
                if normal.z < -0.9:
                    point = face.pointOnFace
                    if point.z < min_z:
                        min_z = point.z
                        bottom_face = face
            except:  # pylint:disable=bare-except
                continue

        if bottom_face is None:
            ui.messageBox('Could not find bottom face — aborting.')
            return

        sketch4 = root_comp.sketches.add(bottom_face)
        # Slightly smaller than the body so pins are clearly a separate block visually.
        sketch4.sketchCurves.sketchLines.addCenterPointRectangle(
            adsk.core.Point3D.create(0, 0, 0),
            adsk.core.Point3D.create(1.0 / 2, 1.1 / 2, 0)
        )
        prof4 = sketch4.profiles.item(0)
        ext4_input = extrudes.createInput(
            prof4, adsk.fusion.FeatureOperations.JoinFeatureOperation
        )
        ext4_input.setDistanceExtent(False, adsk.core.ValueInput.createByReal(0.5))
        ext4_input.participantBodies = [body]
        extrudes.add(ext4_input)

        ui.messageBox('Done — EC11 placeholder body created.')

    except:  # pylint:disable=bare-except
        # Write the error message to the TEXT COMMANDS window.
        app.log(f'Failed:\n{traceback.format_exc()}')
