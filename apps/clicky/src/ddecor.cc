#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <click/config.h>
#include "ddecor.hh"
#include "dstyle.hh"
#include "ddecor.hh"
#include "crouter.hh"
#include <click/confparse.hh>
#include <click/timestamp.hh>
#include <math.h>
extern "C" {
#include "support.h"
}
namespace clicky {

void ddecor::draw(delt *, double *, dcontext &)
{
}

void ddecor::notify(crouter *, delt *, handler_value *)
{
}


/*****
 *
 *
 *
 */

dfullness_decor::dfullness_decor(PermString name, crouter *cr, delt *e,
				 ddecor *next)
    : ddecor(next), _name(name),
      _dfs(cr->ccss()->fullness_style(_name, cr, e)),
      _capacity(-1), _hvalue(-1), _drawn(-1)
{
    if (_dfs->length)
	e->handler_interest(cr, _dfs->length, _dfs->autorefresh > 0, _dfs->autorefresh_period);
    if (_dfs->capacity && !cp_double(_dfs->capacity, &_capacity))
	e->handler_interest(cr, _dfs->capacity, _dfs->autorefresh > 1, _dfs->autorefresh_period);
    notify(cr, e, 0);
}


void dfullness_decor::draw(delt *e, double *sides, dcontext &dcx)
{
    if (_hvalue > 0 && sides) {
	int o = e->orientation();
	double xpos = sides[o];
	sides[o] = fma(std::max(std::min(_hvalue, 1.0), 0.0),
		       sides[o] - sides[o ^ 2], sides[o ^ 2]);
	cairo_set_source_rgba(dcx, _dfs->color[0], _dfs->color[1], _dfs->color[2], _dfs->color[3]);
	cairo_move_to(dcx, sides[1], sides[2]);
	cairo_line_to(dcx, sides[3], sides[2]);
	cairo_line_to(dcx, sides[3], sides[0]);
	cairo_line_to(dcx, sides[1], sides[0]);
	cairo_close_path(dcx);
	cairo_fill(dcx);
	_drawn = _hvalue;
	sides[o] = xpos;
    }
}

void dfullness_decor::notify(crouter *cr, delt *e, handler_value *hv)
{
    handler_value *lv = 0, *cv = 0;
    if (hv) {
	if (hv->handler_name() == _dfs->length)
	    lv = hv;
	else if (hv->handler_name() == _dfs->capacity)
	    cv = hv;
	else
	    return;
    }

    if (!lv)
	lv = cr->hvalues().find(e->flat_name() + "." + _dfs->length);
    if (!cv && _dfs->capacity && _capacity < 0)
	cv = cr->hvalues().find(e->flat_name() + "." + _dfs->capacity);
    
    if (lv && !lv->have_hvalue())
	lv->refresh(cr);
    if (cv && !cv->have_hvalue())
	cv->refresh(cr);

    double l, c;
    _hvalue = -1;
    if (lv && lv->have_hvalue() && cp_double(lv->hvalue(), &l)) {
	if (!_dfs->capacity)
	    _hvalue = l;
	else if (_capacity >= 0)
	    _hvalue = l / _capacity;
	else if (cv && cv->have_hvalue() && cp_double(cv->hvalue(), &c))
	    _hvalue = l / c;
    }

    if ((_drawn < 0) != (_hvalue < 0))
	cr->repaint(*e);
    else
	cr->repaint_if_visible(*e,
		(_drawn - _hvalue) * e->side_length(e->orientation()));
}


/*****
 *
 *
 *
 */

#define ALPHA 0.875
#define LOG_ALPHA -.13353139262452262314

dactivity_decor::dactivity_decor(PermString name, crouter *cr, delt *e,
				 ddecor *next)
    : ddecor(next), _name(name), _cr(cr), _e(e),
      _das(cr->ccss()->activity_style(_name, cr, e)),
      _drawn(0), _decay_source(0)
{
    if (_das->handler)
	e->handler_interest(cr, _das->handler, _das->autorefresh > 0, _das->autorefresh_period, true);
}

dactivity_decor::~dactivity_decor()
{
    if (_decay_source)
	g_source_remove(_decay_source);
}

extern "C" {
static gboolean on_activity_decay(gpointer user_data)
{
    dactivity_decor *da = reinterpret_cast<dactivity_decor *>(user_data);
    return da->on_decay();
}
}

gboolean dactivity_decor::on_decay()
{
    _cr->repaint(*_e);
    return FALSE;
}

static double square(double d)
{
    return d * d;
}

double dactivity_decor::clean_samples(double now, bool want_prev)
{
    double prev_sample = 0;
    unsigned prev_max = (unsigned) -1;
    double max = 0;
    double rate_ago = now - _das->rate_period;

    for (unsigned i = 0; i < _samples.size(); ++i) {
	sample &s = _samples[i];
	if (s.timestamp <= rate_ago || i == 0)
	    prev_sample = s.raw;
	if (s.timestamp <= rate_ago && _das->decay <= 0)
	    s.cooked = 0;
	if (s.cooked) {
	    double val = s.cooked;
	    if (s.timestamp < rate_ago)
		val -= std::min(1., square((rate_ago - s.timestamp) / _das->decay));
	    if (val == 0)
		s.cooked = 0;
	    else if (val > max) {
		max = val;
		if (prev_max != (unsigned) -1)
		    _samples[prev_max].cooked = 0;
		prev_max = i;
	    }
	}
	if (s.timestamp <= rate_ago || i == 0)
	    prev_sample = s.raw;
    }
    while (_samples.size() > 1
	   && _samples.front().cooked == 0
	   && (_samples[1].timestamp <= rate_ago
	       || _samples[0].raw == _samples[1].raw))
	_samples.pop_front();

    return want_prev ? prev_sample : max;
}

void color_interpolate(double *c, const double *c1, double m, const double *c2)
{
    if (c1[3] == 0) {
	memcpy(c, c2, sizeof(double) * 3);
	c[3] = c2[3]*m;
    } else if (c2[3] == 0) {
	memcpy(c, c1, sizeof(double) * 3);
	c[3] = c1[3]*(1-m);
    } else
	for (int i = 0; i < 4; i++)
	    c[i] = c1[i]*(1-m) + c2[i]*m;
}

void dactivity_decor::draw(delt *, double *sides, dcontext &dcx)
{
    if (!sides)
	return;

    double now = Timestamp::now().doubleval();
    if (_das->type == dactivity_absolute)
	_drawn = (_samples.size() ? _samples.back().cooked : 0);
    else
	_drawn = clean_samples(now, false);
    if (_drawn <= 1/128. && !_das->colors[4])
	return;

    int p;
    if (_das->colors.size() == 10)
	p = 0;
    else
	for (p = 0;
	     p < _das->colors.size() - 5 && _drawn >= _das->colors[p + 5];
	     p += 5)
	    /* nada */;

    double *color, colorbuf[4];
    if (fabs(_drawn - _das->colors[p]) < 1 / 128.)
	color = &_das->colors[p+1];
    else {
	color = colorbuf;
	double m = (_drawn - _das->colors[p]) / (_das->colors[p+5] - _das->colors[p]);
	color_interpolate(color, &_das->colors[p+1], m, &_das->colors[p+6]);
    }

    if (color[3]) {
	cairo_set_source_rgba(dcx, color[0], color[1], color[2], color[3]);
	cairo_move_to(dcx, sides[1], sides[2]);
	cairo_line_to(dcx, sides[3], sides[2]);
	cairo_line_to(dcx, sides[3], sides[0]);
	cairo_line_to(dcx, sides[1], sides[0]);
	cairo_close_path(dcx);
	cairo_fill(dcx);
	if (_decay_source)
	    g_source_remove(_decay_source);
	int dto = std::max((int) (80 * _das->decay), 33);
	_decay_source = g_timeout_add(dto, on_activity_decay, this);
    }
}

void dactivity_decor::notify(crouter *cr, delt *e, handler_value *hv)
{
    if (hv->handler_name() != _das->handler)
	return;

    double new_value;
    if (hv->have_hvalue() && cp_double(hv->hvalue(), &new_value)) {
	double now = Timestamp::now().doubleval();
	double cooked;
	if (_das->type == dactivity_absolute || _samples.size() == 0) {
	    _samples.clear();
	    cooked = new_value;
	} else {
	    double prev_value = clean_samples(now, true);
	    cooked = std::max(new_value - prev_value, 0.);
	    //static double first; if (!first) first = now;
	    //if (_e->name() == "c1")
	    //    fprintf(stderr, "@%g: %g -> %g : %g\n", now-first, prev_value, new_value, cooked);
	}
	double range = _das->max_value - _das->min_value;
	cooked = std::min(std::max(cooked - _das->min_value, 0.) / range, 1.);
	_samples.push_back(sample(new_value, cooked, now));
	if (128 * fabs(cooked - _drawn) > range)
	    cr->repaint(*e);
    }
}

}
