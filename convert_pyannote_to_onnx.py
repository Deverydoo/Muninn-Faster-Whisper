#!/usr/bin/env python3
"""
Convert pyannote speaker embedding model to ONNX format
for use with Muninn speaker diarization.
"""

import torch
import torch.onnx
import numpy as np
from pathlib import Path

def convert_pyannote_to_onnx(model_dir: str, output_path: str):
    """
    Convert pyannote PyTorch model to ONNX format.

    Args:
        model_dir: Directory containing pytorch_model.bin and config files
        output_path: Path where ONNX model will be saved
    """
    print(f"[Convert] Loading pyannote model from: {model_dir}")

    try:
        # Import pyannote.audio
        from pyannote.audio import Model

        # Load the pretrained model from local directory
        model = Model.from_pretrained(model_dir)
        model.eval()

        print(f"[Convert] Model loaded successfully")
        print(f"[Convert] Model architecture: {type(model).__name__}")

        # Create dummy input
        # Pyannote expects: [batch_size, num_samples]
        # Use 1 second of audio at 16kHz = 16000 samples
        batch_size = 1
        num_samples = 16000
        dummy_input = torch.randn(batch_size, num_samples)

        print(f"[Convert] Dummy input shape: {dummy_input.shape}")

        # Test the model with dummy input to see output shape
        with torch.no_grad():
            output = model(dummy_input)
            print(f"[Convert] Model output shape: {output.shape}")
            embedding_dim = output.shape[-1]
            print(f"[Convert] Embedding dimension: {embedding_dim}")

        # Export to ONNX
        print(f"[Convert] Exporting to ONNX: {output_path}")

        torch.onnx.export(
            model,                          # Model to export
            dummy_input,                    # Model input (sample)
            output_path,                    # Output file path
            export_params=True,             # Store trained parameters
            opset_version=15,               # ONNX opset version (15 is well-supported)
            do_constant_folding=True,       # Optimize constant folding
            input_names=['waveform'],       # Input tensor name
            output_names=['embedding'],     # Output tensor name
            dynamic_axes={
                'waveform': {1: 'num_samples'},   # Variable length audio
                'embedding': {0: 'batch_size'}     # Variable batch size
            }
        )

        print(f"[Convert] ✓ Successfully exported to: {output_path}")

        # Verify the ONNX model
        import onnx
        onnx_model = onnx.load(output_path)
        onnx.checker.check_model(onnx_model)
        print(f"[Convert] ✓ ONNX model validated successfully")

        # Print model info
        print(f"\n[Convert] Model Information:")
        print(f"  Input name: waveform")
        print(f"  Input shape: [batch, num_samples] (dynamic)")
        print(f"  Output name: embedding")
        print(f"  Output shape: [batch, {embedding_dim}]")
        print(f"  Expected audio: mono, 16kHz")
        print(f"  Model size: {Path(output_path).stat().st_size / 1024 / 1024:.2f} MB")

        return True

    except ImportError as e:
        print(f"[Convert] ERROR: Import failed: {e}")
        print("[Convert] Install with: pip install pyannote.audio torch onnx")
        import traceback
        traceback.print_exc()
        return False

    except Exception as e:
        print(f"[Convert] ERROR: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    import sys

    # Default paths
    model_dir = "models/pyannote"
    output_path = "models/pyannote_embedding.onnx"

    # Allow command-line arguments
    if len(sys.argv) > 1:
        model_dir = sys.argv[1]
    if len(sys.argv) > 2:
        output_path = sys.argv[2]

    print("=" * 60)
    print("PyAnnote to ONNX Converter")
    print("=" * 60)
    print(f"Model directory: {model_dir}")
    print(f"Output path: {output_path}")
    print("=" * 60)
    print()

    success = convert_pyannote_to_onnx(model_dir, output_path)

    if success:
        print()
        print("=" * 60)
        print("Conversion complete!")
        print("=" * 60)
        print()
        print("Next steps:")
        print(f"1. The ONNX model is ready at: {output_path}")
        print("2. Enable diarization in your code:")
        print("   options.enable_diarization = true;")
        print(f"   options.diarization_model_path = \"{output_path}\";")
        print()
        sys.exit(0)
    else:
        print()
        print("=" * 60)
        print("Conversion failed")
        print("=" * 60)
        sys.exit(1)
