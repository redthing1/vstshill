#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = [
#   "typer>=0.15.0",
#   "pillow>=10.0.0",
# ]
# ///

import os
import sys
from pathlib import Path

import typer
from PIL import Image

CONTEXT_SETTINGS = dict(help_option_names=["-h", "--help"])

APP_NAME = "generate-icons"
app = typer.Typer(
    name=APP_NAME,
    help=f"{APP_NAME}: Generate platform-specific icons from PNG source",
    no_args_is_help=True,
    context_settings=CONTEXT_SETTINGS,
    pretty_exceptions_short=True,
    pretty_exceptions_show_locals=False,
)


def generate_bmp_from_png(png_path: Path, bmp_path: Path, size: tuple[int, int] = (32, 32)):
    """Generate BMP icon from PNG source"""
    try:
        with Image.open(png_path) as img:
            # Convert to RGB if needed (BMP doesn't support transparency)
            if img.mode in ('RGBA', 'LA'):
                # Create white background
                background = Image.new('RGB', img.size, (255, 255, 255))
                if img.mode == 'RGBA':
                    background.paste(img, mask=img.split()[-1])  # Use alpha channel as mask
                else:
                    background.paste(img)
                img = background
            elif img.mode != 'RGB':
                img = img.convert('RGB')
            
            # Resize to target size
            img = img.resize(size, Image.Resampling.LANCZOS)
            
            # Save as BMP
            img.save(bmp_path, 'BMP')
            typer.echo(f"Generated BMP: {bmp_path}")
            
    except Exception as e:
        typer.echo(f"Error generating BMP: {e}", err=True)
        raise


def generate_ico_from_png(png_path: Path, ico_path: Path, sizes: list[int] = [16, 32, 48, 64]):
    """Generate ICO file from PNG source with multiple sizes"""
    try:
        images = []
        with Image.open(png_path) as img:
            # Convert to RGBA for ICO
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            for size in sizes:
                resized = img.resize((size, size), Image.Resampling.LANCZOS)
                images.append(resized)
        
        # Save as ICO with multiple sizes
        images[0].save(ico_path, format='ICO', sizes=[(img.width, img.height) for img in images])
        typer.echo(f"Generated ICO: {ico_path}")
        
    except Exception as e:
        typer.echo(f"Error generating ICO: {e}", err=True)
        raise


def generate_icns_from_png(png_path: Path, icns_path: Path):
    """Generate ICNS file from PNG source (macOS)"""
    try:
        # ICNS requires specific sizes
        icns_sizes = [16, 32, 64, 128, 256, 512, 1024]
        temp_dir = icns_path.parent / "iconset.tmp"
        temp_dir.mkdir(exist_ok=True)
        
        with Image.open(png_path) as img:
            # Convert to RGBA
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            # Generate all required sizes
            for size in icns_sizes:
                resized = img.resize((size, size), Image.Resampling.LANCZOS)
                
                # Generate standard resolution
                icon_name = f"icon_{size}x{size}.png"
                resized.save(temp_dir / icon_name, 'PNG')
                
                # Generate @2x (retina) versions for applicable sizes
                if size <= 512:
                    retina_size = size * 2
                    if retina_size <= 1024:
                        retina_resized = img.resize((retina_size, retina_size), Image.Resampling.LANCZOS)
                        retina_name = f"icon_{size}x{size}@2x.png"
                        retina_resized.save(temp_dir / retina_name, 'PNG')
        
        # Use iconutil to create ICNS (macOS only)
        import subprocess
        try:
            subprocess.run(['iconutil', '-c', 'icns', str(temp_dir), '-o', str(icns_path)], 
                         check=True, capture_output=True)
            typer.echo(f"Generated ICNS: {icns_path}")
        except subprocess.CalledProcessError as e:
            typer.echo(f"Error running iconutil: {e}", err=True)
            typer.echo("Note: iconutil is only available on macOS", err=True)
        except FileNotFoundError:
            typer.echo("iconutil not found - creating PNG-based ICNS fallback", err=True)
            # Fallback: just copy the largest PNG
            with Image.open(png_path) as img:
                img = img.resize((512, 512), Image.Resampling.LANCZOS)
                img.save(icns_path.with_suffix('.png'), 'PNG')
        
        # Cleanup
        import shutil
        shutil.rmtree(temp_dir, ignore_errors=True)
        
    except Exception as e:
        typer.echo(f"Error generating ICNS: {e}", err=True)
        raise


def generate_c_header(icon_path: Path, header_path: Path, array_name: str = "app_icon_data"):
    """generate c header file with embedded icon data"""
    try:
        with open(icon_path, 'rb') as f:
            data = f.read()
        
        with open(header_path, 'w') as f:
            f.write(f"// auto-generated icon data from {icon_path.name}\n")
            f.write(f"#pragma once\n\n")
            f.write(f"#include <stddef.h>\n\n")
            f.write(f"extern const unsigned char {array_name}[];\n")
            f.write(f"extern const size_t {array_name.replace('_data', '_size')};\n\n")
            f.write(f"const unsigned char {array_name}[] = {{\n")
            
            # write bytes in groups of 16
            for i in range(0, len(data), 16):
                chunk = data[i:i+16]
                hex_values = [f"0x{b:02x}" for b in chunk]
                f.write(f"    {', '.join(hex_values)}")
                if i + 16 < len(data):
                    f.write(",")
                f.write("\n")
            
            f.write(f"}};\n\n")
            f.write(f"const size_t {array_name.replace('_data', '_size')} = sizeof({array_name});\n")
        
        typer.echo(f"generated c header: {header_path}")
        
    except Exception as e:
        typer.echo(f"error generating c header: {e}", err=True)
        raise


def generate_combined_header(header_path: Path, base_name: str):
    """generate combined header that includes both png and bmp data"""
    try:
        with open(header_path, 'w') as f:
            f.write(f"// auto-generated combined icon header\n")
            f.write(f"#pragma once\n\n")
            f.write(f"#include \"{base_name}_icon_png.h\"\n")
            f.write(f"#include \"{base_name}_icon_bmp.h\"\n\n")
            f.write(f"// cross-platform icon data selection\n")
            f.write(f"#ifdef HAVE_SDL_IMAGE\n")
            f.write(f"static const unsigned char* const app_icon_data = app_icon_png_data;\n")
            f.write(f"static const size_t app_icon_size = app_icon_png_size;\n")
            f.write(f"#else\n")
            f.write(f"static const unsigned char* const app_icon_data = app_icon_bmp_data;\n")
            f.write(f"static const size_t app_icon_size = app_icon_bmp_size;\n")
            f.write(f"#endif\n")
        
        typer.echo(f"generated combined header: {header_path}")
        
    except Exception as e:
        typer.echo(f"error generating combined header: {e}", err=True)
        raise


@app.command()
def cli(
    input_png: str = typer.Argument(..., help="Input PNG file path"),
    output_dir: str = typer.Option(".", "-o", help="Output directory"),
    generate_header: bool = typer.Option(True, "--header/--no-header", help="Generate C header file"),
    verbose: bool = typer.Option(False, "-v", help="Verbose output"),
):
    """Generate platform-specific icons from a PNG source image"""
    
    png_path = Path(input_png)
    if not png_path.exists():
        typer.echo(f"Error: Input PNG file not found: {png_path}", err=True)
        raise typer.Exit(1)
    
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    base_name = png_path.stem
    
    if verbose:
        typer.echo(f"Generating icons from: {png_path}")
        typer.echo(f"Output directory: {output_path}")
    
    # Generate high-res PNG for embedding (with SDL_image)
    icon_png_path = output_path / f"{base_name}_64.png"
    try:
        with Image.open(png_path) as img:
            img = img.resize((64, 64), Image.Resampling.LANCZOS)
            img.save(icon_png_path, 'PNG')
            typer.echo(f"Generated high-res PNG: {icon_png_path}")
    except Exception as e:
        typer.echo(f"Error generating high-res PNG: {e}", err=True)
    
    # Generate BMP for fallback (without SDL_image)
    bmp_path = output_path / f"{base_name}_32.bmp"
    generate_bmp_from_png(png_path, bmp_path, (32, 32))
    
    # Generate ICO for Windows
    ico_path = output_path / f"{base_name}.ico"
    generate_ico_from_png(png_path, ico_path)
    
    # Generate ICNS for macOS
    icns_path = output_path / f"{base_name}.icns"
    generate_icns_from_png(png_path, icns_path)
    
    # Generate large PNG for Linux
    linux_png_path = output_path / f"{base_name}_64.png"
    try:
        with Image.open(png_path) as img:
            img = img.resize((64, 64), Image.Resampling.LANCZOS)
            img.save(linux_png_path, 'PNG')
            typer.echo(f"Generated Linux PNG: {linux_png_path}")
    except Exception as e:
        typer.echo(f"Error generating Linux PNG: {e}", err=True)
    
    # generate c header for embedding - create dual version for png and bmp
    if generate_header:
        # generate png header (high quality)
        png_header_path = output_path / f"{base_name}_icon_png.h"
        generate_c_header(icon_png_path, png_header_path, "app_icon_png_data")
        
        # generate bmp header (fallback)
        bmp_header_path = output_path / f"{base_name}_icon_bmp.h"
        generate_c_header(bmp_path, bmp_header_path, "app_icon_bmp_data")
        
        # generate combined header
        combined_header_path = output_path / f"{base_name}_icon.h"
        generate_combined_header(combined_header_path, base_name)
    
    typer.echo("Icon generation complete!")


if __name__ == "__main__":
    app()