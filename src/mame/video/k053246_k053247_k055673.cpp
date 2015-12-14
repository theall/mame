// license:BSD-3-Clause
// copyright-holders:David Haywood, Olivier Galibert
/***************************************************************************/
/*                                                                         */
/*                                 053246                                  */
/*                          with 053247 or 055673                          */
/*  is the 053247 / 055673 choice just a BPP change like the tilemaps?     */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
/* later Konami GX board replaces the 053246 with a 058142 */

/*

053247/053246
-------------
Sprite generators. Nothing is known about their external interface.
The sprite RAM format is very similar to the 053245.

053246 memory map (but the 053247 sees and processes them too):
000-001 W  global X offset
002-003 W  global Y offset
004     W  low 8 bits of the ROM address to read
005     W  bit 0 = flip screen X
           bit 1 = flip screen Y
           bit 2 = unknown
           bit 4 = interrupt enable
           bit 5 = unknown
006-007 W  high 16 bits of the ROM address to read

???-??? R  reads data from the gfx ROMs (16 bits in total). The address of the
           data is determined by the registers above


*/

#include "k053246_k053247_k055673.h"

const device_type K053246_053247 = &device_creator<k053246_053247_device>;
const device_type K053246_055673 = &device_creator<k053246_055673_device>;

DEVICE_ADDRESS_MAP_START(objset1, 16, k053246_055673_device)
	AM_RANGE(0x00, 0x01) AM_WRITE(hscr_w)
	AM_RANGE(0x02, 0x03) AM_WRITE(vscr_w)
	AM_RANGE(0x04, 0x05) AM_WRITE(oms_w)
	AM_RANGE(0x06, 0x07) AM_WRITE(ocha_w)
ADDRESS_MAP_END

DEVICE_ADDRESS_MAP_START(objset2, 16, k053246_055673_device)
	AM_RANGE(0x00, 0x07) AM_WRITE(atrbk_w)
	AM_RANGE(0x08, 0x0b) AM_WRITE(vrcbk_w)
	AM_RANGE(0x0c, 0x0d) AM_WRITE(opset_w)
ADDRESS_MAP_END

DEVICE_ADDRESS_MAP_START(objset1_8, 8, k053246_055673_device)
ADDRESS_MAP_END

k053246_055673_device::k053246_055673_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, K053246_055673, "053246/055673 Sprite Generator Combo", tag, owner, clock, "k055673", __FILE__),
	  device_gfx_interface(mconfig, *this),
	  m_dmairq_cb(*this),
	  m_dmaact_cb(*this),
	  m_region(*this, DEVICE_SELF)
{
	m_is_053247 = false;
}

k053246_055673_device::k053246_055673_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, uint32_t clock, const char *shortname, const char *source)
	: device_t(mconfig, type, name, tag, owner, clock, shortname, source),
	  device_gfx_interface(mconfig, *this),
	  m_dmairq_cb(*this),
	  m_dmaact_cb(*this),
	  m_region(*this, DEVICE_SELF)
{
	m_is_053247 = false;
}

void k053246_055673_device::set_info(const char *palette_tag, const char *spriteram_tag)
{
	static_set_palette(*this, palette_tag);
	m_spriteram_tag = spriteram_tag;
}

void k053246_055673_device::set_wiring_cb(wiring_delegate cb)
{
	m_wiring_cb = cb;
}

void k053246_055673_device::device_start()
{
	m_dmaact_cb.resolve_safe();
	m_dmairq_cb.resolve_safe();

	// Can't use a required_* because the pointer type varies
	m_spriteram = owner()->memshare(m_spriteram_tag);
	if(!m_spriteram)
		fatalerror("Spriteram shared memory '%s' does not exist\n", m_spriteram_tag);
	if(m_spriteram->bytewidth() > 4)
		fatalerror("Spriteram shared memory '%s' byte width is %d, which is too large\n", m_spriteram_tag, m_spriteram->bytewidth());

	m_timer_objdma = timer_alloc(0);
}

void k053246_055673_device::device_reset()
{
	m_ocha = 0;
	m_hscr = 0;
	m_vscr = 0;
	memset(m_atrbk, 0, sizeof(m_atrbk));
	memset(m_vrcbk, 0, sizeof(m_vrcbk));
	m_oms = 0;
	m_coreg = 0;
	m_opset = 0;

	m_timer_objdma_state = OD_IDLE;
	m_timer_objdma->adjust(attotime::never);

	decode_sprite_roms();
}

void k053246_055673_device::set_objatrsel(bool active)
{
}

void k053246_055673_device::set_objatrclr(bool active)
{
}

void k053246_055673_device::set_objcha(bool active)
{
}

WRITE16_MEMBER(k053246_055673_device::hscr_w)
{
	uint16_t old = m_hscr;
	COMBINE_DATA(&m_hscr);
	if(m_hscr != old) {
		logerror("hscr_w %04x\n", m_hscr);
		logerror("TIMINGS OH %4d\n", m_hscr & 0x3ff);
	}
}

WRITE16_MEMBER(k053246_055673_device::vscr_w)
{
	uint16_t old = m_vscr;
	COMBINE_DATA(&m_vscr);
	if(m_vscr != old) {
		logerror("vscr_w %04x\n", m_vscr);
		logerror("TIMINGS OV %4d\n", m_vscr & 0x3ff);
	}
}

WRITE16_MEMBER(k053246_055673_device::oms_w)
{
	if(ACCESSING_BITS_8_15 && (data >> 8) != (m_ocha & 0xff))
		m_ocha = (m_ocha & 0xffff00) | (data >> 8);

	if(ACCESSING_BITS_0_7 && (data & 0x3f) != (m_oms & 0x3f)) {
		uint8_t diff = (data ^ m_oms) & 0x2f;
		m_oms = data & 0x3f;
		if(!(m_oms & 0x10) && m_dmairq_on) {
			m_dmairq_on = false;
			m_dmairq_cb(CLEAR_LINE);
		}

		if(diff)
			logerror("oms_w sd0en=%s dma=%s res=%s mode=%d flip=%c%c\n",
					 m_oms & 0x20 ? "normal" : "shadow",
					 m_oms & 0x10 ? "on" : "off",
					 m_oms & 0x08 ? "high" : "normal",
					 m_oms & 0x04 ? 16 : 8,
					 m_oms & 0x02 ? 'y' : '-',
					 m_oms & 0x01 ? 'x' : '-');
	}
}

WRITE16_MEMBER(k053246_055673_device::ocha_w)
{
	m_ocha = (m_ocha & 0x0000ff) | ((data << 8) & 0xffff00);
}

WRITE16_MEMBER(k053246_055673_device::atrbk_w)
{
	if(m_atrbk[offset] != (data & 0xfff)) {
		m_atrbk[offset] = data & 0xfff;
		logerror("atrbk_w %d wr=%s trans=0-%x mix=%c%c bri=%c%c\n",
				 offset,
				 m_atrbk[offset] & 0x800 ? "on" : "off",
				 0x7f >> (((~data) >> 8) & 7),
				 m_atrbk[offset] & 0x040 ? '0' : m_atrbk[offset] & 0x080 ? 'c' : m_atrbk[offset] & 0x020 ? '1' : '0',
				 m_atrbk[offset] & 0x080 ? 'c' : m_atrbk[offset] & 0x010 ? '1' : '0',
				 m_atrbk[offset] & 0x004 ? '0' : m_atrbk[offset] & 0x008 ? 'c' : m_atrbk[offset] & 0x002 ? '1' : '0',
				 m_atrbk[offset] & 0x008 ? 'c' : m_atrbk[offset] & 0x001 ? '1' : '0');

	}
}

WRITE16_MEMBER(k053246_055673_device::vrcbk_w)
{
	if(ACCESSING_BITS_8_15 && m_vrcbk[offset*2+1] != ((data >> 8) & 0xf)) {
		m_vrcbk[offset*2+1] = (data >> 8) & 0x0f;
		logerror("vrcbk[%d] = %x\n", offset*2+1, m_vrcbk[offset*2+1]);
	}

	if(ACCESSING_BITS_0_7 && m_vrcbk[offset*2] != (data & 0xf)) {
		m_vrcbk[offset*2] = data & 0x0f;
		logerror("vrcbk[%d] = %x\n", offset*2, m_vrcbk[offset*2]);
	}
}

WRITE16_MEMBER(k053246_055673_device::opset_w)
{
	if(ACCESSING_BITS_8_15 && m_coreg != ((data >> 8) & 0xf)) {
		m_coreg = (data >> 8) & 0xf;
		logerror("coreg_w %x\n", m_coreg);
	}
	if(ACCESSING_BITS_0_7 && m_opset != (data & 0xff)) {
		static int bpp[8] = { 4, 5, 6, 7, 8, 8, 8, 8 };
		m_opset = data;
		decode_sprite_roms();
		logerror("opset_w rom=%s wrap=%d sdsel=%s pri=%s objwr=%s bpp=%d\n",
				 m_opset & 0x80 ? "readback" : "normal",
				 m_opset & 0x40 ? 512 : 1024,
				 m_opset & 0x20 ? "pri" : "nopri",
				 m_opset & 0x10 ? "higher" : "lower",
				 m_opset & 0x08 ? "invert" : "normal",
				 bpp[m_opset & 7]);
	}
}

READ8_MEMBER(k053246_055673_device::rom8_r)
{
	uint32_t off = ((m_vrcbk[(m_ocha >> 20) & 3] << 22) | ((m_ocha & 0xffffc) << 2) | offset) & (m_region->bytes() - 1);
	const uint8_t *rom = m_region->base() + (off^1);
	return rom[0];
}

READ16_MEMBER(k053246_055673_device::rom16_r)
{
	uint32_t off = ((m_vrcbk[(m_ocha >> 20) & 3] << 22) | ((m_ocha & 0xffffc) << 2) | (offset << 1)) & (m_region->bytes() - 1);
	const uint8_t *rom = m_region->base() + off;
	return (rom[1] << 8) | rom[0];
}

READ32_MEMBER(k053246_055673_device::rom32_r)
{
	uint32_t off = ((m_vrcbk[(m_ocha >> 20) & 3] << 22) | ((m_ocha & 0xffffc) << 2) | (offset << 2)) & (m_region->bytes() - 1);
	const uint8_t *rom = m_region->base() + off;
	return (rom[1] << 24) | (rom[0] << 16) | (rom[3] << 8) | rom[2];
}


WRITE_LINE_MEMBER(k053246_055673_device::vblank_w)
{
	if(state) {
		if(m_oms & 0x10) {
			if(m_dmairq_on) {
				m_dmairq_on = false;
				m_dmairq_cb(CLEAR_LINE);
			}
			m_timer_objdma_state = OD_WAIT_START;
			m_timer_objdma->adjust(attotime::from_ticks(256, clock()));
		} else
			memset(m_sram, 0, sizeof(m_sram));
	}
}

void k053246_055673_device::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	switch(m_timer_objdma_state) {
	case OD_WAIT_START:
		m_timer_objdma_state = OD_WAIT_END;
		m_timer_objdma->adjust(attotime::from_ticks(2048, clock()));
		m_dmaact_cb(ASSERT_LINE);
		switch(m_spriteram->bytewidth()) {
		case 1: {
			const uint8_t *base = (const uint8_t *)m_spriteram->ptr();
			for(uint32_t i=0; i<0x800; i++)
				m_sram[i] = (base[2*i] << 8) | base[2*i+1];
			break;
		}
		case 2: {
			memcpy(m_sram, m_spriteram->ptr(), 0x1000);
			break;
		}
		case 4: {
			const uint32_t *base = (const uint32_t *)m_spriteram->ptr();
			for(uint32_t i=0; i<0x800; i+=2) {
				m_sram[i] = base[i>>1] >> 16;
				m_sram[i+1] = base[i>>1];
			}
			break;
		}
		}
		break;

	case OD_WAIT_END:
		m_timer_objdma_state = OD_IDLE;
		m_timer_objdma->adjust(attotime::never);
		m_dmaact_cb(CLEAR_LINE);
		if((m_oms & 0x10) && !m_dmairq_on) {
			m_dmairq_on = true;
			m_dmairq_cb(ASSERT_LINE);
		}
		break;
	}
}

void k053246_055673_device::generate_tile_layout(tile_layout *tl, int &tl_count, int minp, int maxp, int screen_center, int width_order, int zoom, const int *tile_id_steps, bool flip, bool mirror, bool gflip)
{
	uint32_t pxor[2];
	int32_t step[2];
	uint32_t pxor_test = 1 << (width_order+3+6);
	uint32_t xor_mask = (1 << (width_order+4+6)) - 1;

	tl_count = 0;

	if(gflip) {
		screen_center = 1023 - screen_center;
		flip = !flip;
	}

	int32_t tpos = 1 << (width_order+3+6);

	if(mirror) {
		pxor[0] = 0;
		pxor[1] = xor_mask;
		step[0] = zoom;
		step[1] = -zoom;
	} else if(flip) {
		pxor[0] = xor_mask;
		pxor[1] = xor_mask;
		step[0] = -zoom;
		step[1] = -zoom;
	} else {
		pxor[0] = 0;
		pxor[1] = 0;
		step[0] = zoom;
		step[1] = zoom;
	}

	if(0)
		fprintf(stderr, "generate span=(%d-%d) center=%d width=%d zoom=%d%s%s\n", minp, maxp, screen_center, 1<<(4+width_order), zoom, flip ? " (flipped)" : "", mirror ? " (mirrored)" : "");

	int32_t limit = 1 << (width_order+4+6);
	int spos = screen_center;
	if(spos < minp) {
		while(tpos < limit && spos < minp) {
			spos++;
			tpos += zoom;
		}
		if(tpos >= limit)
			return;
	} else {
		while(tpos > 0 && spos > minp) {
			spos--;
			tpos -= zoom;
		}
		if(tpos < 0) {
			tpos += zoom;
			spos++;
		}
	}
	if(spos > maxp)
		return;
	int ct = tpos >> (4+6);
	tl[tl_count].sc_min = spos;
	if(tpos & pxor_test) {
		uint32_t pp = tpos ^ pxor[1];
		tl[tl_count].tile_min = pp & 0x3ff;
		tl[tl_count].step = step[1];
		tl[tl_count].tileid_delta = tile_id_steps[pp >> 10];
	} else {
		uint32_t pp = tpos ^ pxor[0];
		tl[tl_count].tile_min = pp & 0x3ff;
		tl[tl_count].step = step[0];
		tl[tl_count].tileid_delta = tile_id_steps[pp >> 10];
	}

	for(;;) {
		tpos += zoom;
		if(tpos >= limit || spos == maxp) {
			tl[tl_count].sc_max = spos;
			tl_count++;
			break;
		}
		spos++;
		int nct = tpos >> (4+6);
		if(nct != ct) {
			tl[tl_count].sc_max = spos-1;
			tl_count++;
			ct = nct;
			tl[tl_count].sc_min = spos;
			if(tpos & pxor_test) {
				uint32_t pp = tpos ^ pxor[1];
				tl[tl_count].tile_min = pp & 0x3ff;
				tl[tl_count].step = step[1];
				tl[tl_count].tileid_delta = tile_id_steps[pp >> 10];
			} else {
				uint32_t pp = tpos ^ pxor[0];
				tl[tl_count].tile_min = pp & 0x3ff;
				tl[tl_count].step = step[0];
				tl[tl_count].tileid_delta = tile_id_steps[pp >> 10];
			}
		}
	}

	//	for(int i=0; i<tl_count; i++)
	//		fprintf(stderr, " [(%d, %d) %x.%x.%02x %d %d]", tl[i].sc_min, tl[i].sc_max, tl[i].tile_min >> 10, (tl[i].tile_min >> 6) & 15, tl[i].tile_min & 0x3f, tl[i].step, tl[i].tileid_delta);
	//	fprintf(stderr, "\n");
}

template<uint16_t mask> void k053246_055673_device::draw_tile_keep_shadow(bitmap_ind16 *bcolor, bitmap_ind16 *battr,
																		gfx_element *g, uint32_t tile_base_id,
																		const tile_layout &lx, const tile_layout &ly,
																		uint16_t attr, uint16_t palette, uint8_t trans_mask,
																		int offx, int offy)
{
	uint32_t tid = tile_base_id ^ lx.tileid_delta ^ ly.tileid_delta;
	const uint8_t *data = g->get_data(tid);
	uint32_t typ = ly.tile_min;
	for(int y = ly.sc_min; y <= ly.sc_max; y++) {
		const uint8_t *data1 = data + 16*(typ >> 6);		
		uint16_t *bc = &bcolor->pix16(y+offy, lx.sc_min+offx);
		uint16_t *ba = &battr ->pix16(y+offy, lx.sc_min+offx);
		uint32_t txp = lx.tile_min;
		for(int x = lx.sc_min; x <= lx.sc_max; x++) {
			uint8_t col = data1[txp >> 6];
			if(col & trans_mask) {
				*bc++ = palette | col;
				*ba = (*ba & mask) | attr;
				ba++;
			} else {
				bc++;
				ba++;
			}
			txp += lx.step;
		}
		typ += ly.step;
	}
}

void k053246_055673_device::draw_tile_force_shadow(bitmap_ind16 *bcolor, bitmap_ind16 *battr,
												   gfx_element *g, uint32_t tile_base_id,
												   const tile_layout &lx, const tile_layout &ly,
												   uint16_t attr, uint16_t palette, uint8_t trans_mask,
												   int offx, int offy)
{
	uint32_t tid = tile_base_id + lx.tileid_delta + ly.tileid_delta;
	const uint8_t *data = g->get_data(tid);
	uint32_t typ = ly.tile_min;
	for(int y = ly.sc_min; y <= ly.sc_max; y++) {
		const uint8_t *data1 = data + 16*(typ >> 6);		
		uint16_t *bc = &bcolor->pix16(y+offy, lx.sc_min+offx);
		uint16_t *ba = &battr ->pix16(y+offy, lx.sc_min+offx);
		uint32_t txp = lx.tile_min;
		for(int x = lx.sc_min; x <= lx.sc_max; x++) {
			uint8_t col = data1[txp >> 6];
			if(col & trans_mask) {
				*bc++ = palette | col;
				*ba++ = attr;

			} else {
				bc++;
				ba++;
			}
			txp += lx.step;
		}
		typ += ly.step;
	}
}

template<uint16_t mask> void k053246_055673_device::draw_tile_keep_detect_shadow(bitmap_ind16 *bcolor, bitmap_ind16 *battr,
																			   gfx_element *g, uint32_t tile_base_id,
																			   const tile_layout &lx, const tile_layout &ly,
																			   uint16_t noshadow_attr, uint16_t shadow_attr,
																			   uint16_t palette, uint8_t trans_mask, uint8_t shadow_color,
																			   int offx, int offy)
{
	uint32_t tid = tile_base_id | lx.tileid_delta | ly.tileid_delta;
	const uint8_t *data = g->get_data(tid);
	uint32_t typ = ly.tile_min;
	for(int y = ly.sc_min; y <= ly.sc_max; y++) {
		const uint8_t *data1 = data + 16*(typ >> 6);		
		uint16_t *bc = &bcolor->pix16(y+offy, lx.sc_min+offx);
		uint16_t *ba = &battr ->pix16(y+offy, lx.sc_min+offx);
		uint32_t txp = lx.tile_min;
		for(int x = lx.sc_min; x <= lx.sc_max; x++) {
			uint8_t col = data1[txp >> 6];
			if((col & shadow_color) == shadow_color) {
				*bc++ = palette; // Hypothesis: a sprite cannot shadow another sprite
				*ba++ = shadow_attr;
				
			} else if(col & trans_mask) {
				*bc++ = palette | col;
				*ba = (*ba & mask) | noshadow_attr;
				ba++;

			} else {
				bc++;
				ba++;
			}
			txp += lx.step;
		}
		typ += ly.step;
	}
}

void k053246_055673_device::draw_tile_force_detect_shadow(bitmap_ind16 *bcolor, bitmap_ind16 *battr,
														  gfx_element *g, uint32_t tile_base_id,
														  const tile_layout &lx, const tile_layout &ly,
														  uint16_t noshadow_attr, uint16_t shadow_attr,
														  uint16_t palette, uint8_t trans_mask, uint8_t shadow_color,
														  int offx, int offy)
{
	uint32_t tid = tile_base_id | lx.tileid_delta | ly.tileid_delta;
	const uint8_t *data = g->get_data(tid);
	uint32_t typ = ly.tile_min;
	for(int y = ly.sc_min; y <= ly.sc_max; y++) {
		const uint8_t *data1 = data + 16*(typ >> 6);		
		uint16_t *bc = &bcolor->pix16(y+offy, lx.sc_min+offx);
		uint16_t *ba = &battr ->pix16(y+offy, lx.sc_min+offx);
		uint32_t txp = lx.tile_min;
		for(int x = lx.sc_min; x <= lx.sc_max; x++) {
			uint8_t col = data1[txp >> 6];
			if((col & shadow_color) == shadow_color) {
				*bc++ = palette;
				*ba++ = shadow_attr;
				
			} else if(col & trans_mask) {
				*bc++ = palette | col;
				*ba++ = noshadow_attr;

			} else {
				bc++;
				ba++;
			}
			txp += lx.step;
		}
		typ += ly.step;
	}
}

void k053246_055673_device::bitmap_update(bitmap_ind16 *bcolor, bitmap_ind16 *battr, const rectangle &cliprect)
{
	static const int xoff[16] = { 0, 1, 4,  5, 16, 17, 20, 21, 0, 1, 4,  5, 16, 17, 20, 21 };
	static const int yoff[16] = { 0, 2, 8, 10, 32, 34, 40, 42, 0, 2, 8, 10, 32, 34, 40, 42 };
	static const int unwrap[32] = { 0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 6, 7, 4, 5, 4, 5, 6, 7, 6, 7 };
	uint16_t wrmask = m_opset & 0x08 ? 0x0800 : 0x000;
	int bpp;
	if(m_opset & 0x04)
		bpp = 8;
	else
		bpp = 4 | (m_opset & 0x03);

	uint8_t shadow_color = (1 << bpp) - 1;
	uint32_t coreg = (m_coreg << 12) & (0xf00 << bpp);
	
	bcolor->fill(0, cliprect);
	battr->fill(0, cliprect);

	int offx = 0, offy = 0;

	// gokuparo = -23/22
	//	int offx = -13;
	//	int offy = -21;
	offx = -23;
	offy = 22;

	tile_layout lx[16], ly[16];
	int tlx, tly;

	// The sprites are drawn in priority order.  This is done by
	// dma-ing to specific per-pri slots.  We do it here, but it's
	// identical.  Only one sprite per level is drawn, the last one in
	// memory order.
	int bucket[256];
	memset(bucket, 0xff, sizeof(bucket));

	uint8_t prixor = m_opset & 0x10 ? 0x00 : 0xff;
	for(int i=0; i<0x800; i+=8) {
		const uint16_t *spr = m_sram + i;
		if(!(spr[0] & 0x8000))
			continue;
		if(bucket[(spr[0] ^ prixor) & 0xff] != -1)
			logerror("collision on level %02x\n", spr[0] & 0xff);
		bucket[(spr[0] ^ prixor) & 0xff] = i;
	}

	gfx_element *g = gfx(0);
	for(int i=0; i<256; i++) {
		if(bucket[i] == -1)
			continue;
		const uint16_t *spr = m_sram + bucket[i];
			
		uint16_t atrbk = m_is_053247 ? 0x0000 : m_atrbk[(spr[6] >> 8) & 3];
		if((atrbk ^ wrmask) & 0x0800)
			continue;
			
		int osx = (spr[0] >>  8) & 3;
		int osy = (spr[0] >> 10) & 3;

		uint32_t tile_base_id = spr[1];
		if(!m_is_053247)
			tile_base_id = ((tile_base_id & 0x3fff) | (m_vrcbk[(tile_base_id >> 14) & 3] << 14));

		int dx = unwrap[tile_base_id & 0x1f];
		int dy = unwrap[(tile_base_id >> 1) & 0x1f];
		tile_base_id &= ~0x3f;

		if(0)
			logerror("sprite %02x size=%dx%d tile=%05x  -- %04x %04x %04x %04x %04x %04x %04x %04x\n", bucket[i] >> 3, 1<<osx, 1<<osy, tile_base_id, spr[0], spr[1], spr[2], spr[3], spr[4], spr[5], spr[6], spr[7]);
			
		int zoom = spr[4] & 0x3ff;
		generate_tile_layout(ly, tly, cliprect.min_y-offy, cliprect.max_y-offy, (-spr[2] - m_vscr) & 0x3ff, osy, zoom, yoff+dy, spr[0] & 0x2000, spr[6] & 0x8000, m_oms & 2);
		if(!tly)
			continue;
			
		if(!(spr[0] & 0x4000))
			zoom = spr[5] & 0x3ff;
		generate_tile_layout(lx, tlx, cliprect.min_x-offx, cliprect.max_x-offx, (spr[3] - m_hscr) & 0x3ff, osx, zoom, xoff+dx, spr[0] & 0x1000, spr[6] & 0x4000, m_oms & 1);
		
		if(!tlx)
			continue;

		uint32_t info_ns, info_s;
		uint16_t trans_mask;
		if(!m_is_053247) {
			info_s = ((spr[6] & 0xf00) << 8) | ((spr[6] & 0x0ff) << bpp) | coreg;
			info_ns = info_s & (m_oms & 0x20 ? 0x3ffff : 0x7ffff);
			trans_mask = (0xff << ((atrbk >> 8) & 7)) & shadow_color;
		} else {
			info_s  = (spr[6] & 0xfff) << 4;
			info_ns = info_s & (m_oms & 0x20 ? 0x3fff : 0x7fff);
			trans_mask = shadow_color;
		}

#if 0
		color = (spr[6] << bpp) & 0xff;
		attr = ((spr[6] & 0xff) >> (8-bpp)) | coreg;
#endif
		uint16_t color, shadow_attr, noshadow_attr;

		m_wiring_cb(info_ns, color, noshadow_attr);
		m_wiring_cb(info_s,  color, shadow_attr);

		logerror("tbid %x color %x ns %x s %x\n", tile_base_id, color, noshadow_attr, shadow_attr);

		//		uint16_t color          = info_ns & 0x1ff;
		//		uint16_t noshadow_attr  = ((info_ns & 0xc000) >> 6) | ((info_ns >> 8) & 0x3e);
		//		uint16_t shadow_attr    = ((info_s  & 0xc000) >> 6) | ((info_s  >> 8) & 0x3e);

		if((m_opset & 0x20) || !(m_oms & 0x20)) {
			if(!(spr[6] & 0x0c00))
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_force_shadow(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, color, trans_mask, offx, offy);
			else
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_force_detect_shadow(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, shadow_attr, color, trans_mask, shadow_color, offx, offy);

		} else if(m_oms & 0x20)  {
			if(!(spr[6] & 0x0c00))
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_keep_shadow<0x0300>(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, color, trans_mask, offx, offy);
			else
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_keep_detect_shadow<0x0300>(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, shadow_attr, color, trans_mask, shadow_color, offx, offy);

		} else {
			if(!(spr[6] & 0x0800))
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_keep_shadow<0x0100>(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, color, trans_mask, offx, offy);
			else
				for(int y=0; y<tly; y++)
					for(int x=0; x<tlx; x++)
						draw_tile_keep_detect_shadow<0x0100>(bcolor, battr, g, tile_base_id, lx[x], ly[y], noshadow_attr, shadow_attr, color, trans_mask, shadow_color, offx, offy);
		}
	}
}

void k053246_055673_device::decode_sprite_roms()
{
	gfx_layout gfx_layouts[1];
	gfx_decode_entry gfx_entries[2];

	int bpp;

	if(m_is_053247)
		bpp = 4;
	else if(m_opset & 0x04)
		bpp = 8;
	else
		bpp = 4 | (m_opset & 0x03);

	logerror("Decoding sprite roms as %d bpp %s\n", bpp, m_is_053247 ? "chunky" : "planar");

	int bits_per_block = m_is_053247 ? 32 : 64;

	gfx_layouts[0].width = 16;
	gfx_layouts[0].height = 16;
	gfx_layouts[0].total = m_region->bytes() / (bits_per_block*16*2/8);
	gfx_layouts[0].planes = bpp;

	if(m_is_053247) {
		// Chunky format, 32 bits per line, 4bpp
		for(int j=0; j<bpp; j++)
			gfx_layouts[0].planeoffset[j] = j;
		for(int j=0; j<16; j++) {
			gfx_layouts[0].xoffset[j] = 4*(j & 7) + (j & 8 ? bits_per_block : 0);
			gfx_layouts[0].yoffset[j] = j*2*bits_per_block;
		}

	} else {
		// Planar format, 64 bits per line (32 to 64 actually used)
		for(int j=0; j<bpp; j++)
			gfx_layouts[0].planeoffset[bpp-1-j] = 8*j;
		for(int j=0; j<16; j++) {
			gfx_layouts[0].xoffset[j] = (j & 7) + (j & 8 ? bits_per_block : 0);
			gfx_layouts[0].yoffset[j] = j*2*bits_per_block;
		}
	}
	gfx_layouts[0].extxoffs = nullptr;
	gfx_layouts[0].extyoffs = nullptr;
	gfx_layouts[0].charincrement = bits_per_block * 2 * 16;

	gfx_entries[0].memory_region = tag();
	gfx_entries[0].start = 0;
	gfx_entries[0].gfxlayout = gfx_layouts;
	gfx_entries[0].color_codes_start = 0;
	gfx_entries[0].total_color_codes = palette().entries() >> bpp;
	gfx_entries[0].flags = 0;

	gfx_entries[1].gfxlayout = nullptr;

	decode_gfx(gfx_entries);
}


k053246_053247_device::k053246_053247_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: k053246_055673_device(mconfig, K053246_053247, "K053246/053247 Sprite Generator Combo", tag, owner, clock, "k053247", __FILE__)
{
	m_is_053247 = true;
}

READ8_MEMBER(k053246_053247_device::rom8_r)
{
	uint32_t off = ((m_ocha << 1) | offset) & (m_region->bytes() - 1);
	const uint8_t *rom = m_region->base() + (off^1);
	return rom[0];
}

READ16_MEMBER(k053246_053247_device::rom16_r)
{
	uint32_t off = (m_ocha << 1) & (m_region->bytes() - 1);
	const uint8_t *rom = m_region->base() + off;
	return (rom[1] << 8) | rom[0];
}

READ32_MEMBER(k053246_053247_device::rom32_r)
{
	abort();
}
