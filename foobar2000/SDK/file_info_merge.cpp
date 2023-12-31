#include "foobar2000-sdk-pch.h"
#include "file_info.h"

static t_size merge_tags_calc_rating_by_index(const file_info & p_info,t_size p_index) {
	t_size n,m = p_info.meta_enum_value_count(p_index);
	t_size ret = 0;
	for(n=0;n<m;n++)
		ret += strlen(p_info.meta_enum_value(p_index,n)) + 10;//yes, strlen on utf8 data, plus a slight bump to prefer multivalue over singlevalue w/ separator
	return ret;
}
#if 0
static t_size merge_tags_calc_rating(const file_info & p_info,const char * p_field) {
	t_size field_index = p_info.meta_find(p_field);
	if (field_index != ~0) {
		return merge_tags_calc_rating_by_index(p_info,field_index);
	} else {
		return 0;
	}
}

static void merge_tags_copy_info(const char * field,const file_info * from,file_info * to)
{
	const char * val = from->info_get(field);
	if (val) to->info_set(field,val);
}
#endif

namespace {
	struct meta_merge_entry {
		meta_merge_entry() : m_rating(0) {}
		t_size m_rating;
		pfc::array_t<const char *> m_data;
	};

	class meta_merge_map_enumerator {
	public:
		meta_merge_map_enumerator(file_info & p_out) : m_out(p_out) {
			m_out.meta_remove_all();
		}
		void operator() (const char * p_name, const meta_merge_entry & p_entry) {
			if (p_entry.m_data.get_size() > 0) {
				t_size index = m_out.__meta_add_unsafe(p_name,p_entry.m_data[0]);
				for(t_size walk = 1; walk < p_entry.m_data.get_size(); ++walk) {
					m_out.meta_add_value(index,p_entry.m_data[walk]);
				}
			}
		}
	private:
		file_info & m_out;
	};
}

static void merge_meta(file_info & p_out,const pfc::list_base_const_t<const file_info*> & p_in) {
	pfc::map_t<const char *,meta_merge_entry,pfc::comparator_stricmp_ascii> map;
	for(t_size in_walk = 0; in_walk < p_in.get_count(); in_walk++) {
		const file_info & in = * p_in[in_walk];
		for(t_size meta_walk = 0, meta_count = in.meta_get_count(); meta_walk < meta_count; meta_walk++ ) {
			meta_merge_entry & entry = map.find_or_add(in.meta_enum_name(meta_walk));
			t_size rating = merge_tags_calc_rating_by_index(in,meta_walk);
			if (rating > entry.m_rating) {
				entry.m_rating = rating;
				const t_size value_count = in.meta_enum_value_count(meta_walk);
				entry.m_data.set_size(value_count);
				for(t_size value_walk = 0; value_walk < value_count; value_walk++ ) {
					entry.m_data[value_walk] = in.meta_enum_value(meta_walk,value_walk);
				}
			}
		}
	}

    meta_merge_map_enumerator en(p_out);
	map.enumerate(en);
}

void file_info::merge(const pfc::list_base_const_t<const file_info*> & p_in)
{
	t_size in_count = p_in.get_count();
	if (in_count == 0)
	{
		meta_remove_all();
		return;
	}
	else if (in_count == 1)
	{
		const file_info * info = p_in[0];

		copy_meta(*info);

		set_replaygain(replaygain_info::g_merge(get_replaygain(),info->get_replaygain()));

		overwrite_info(*info);

		//copy_info_single_by_name(*info,"tagtype");
		
		return;
	}
	
	merge_meta(*this,p_in);

	{
		pfc::string8_fastalloc tagtype;
		replaygain_info rg = get_replaygain();
		t_size in_ptr;
		for(in_ptr = 0; in_ptr < in_count; in_ptr++ )
		{
			const file_info * info = p_in[in_ptr];
			rg = replaygain_info::g_merge(rg, info->get_replaygain());
			t_size field_ptr, field_max = info->info_get_count();
			for(field_ptr = 0; field_ptr < field_max; field_ptr++ )
			{
				const char * field_name = info->info_enum_name(field_ptr), * field_value = info->info_enum_value(field_ptr);
				if (*field_value)
				{
					if (!pfc::stricmp_ascii(field_name,"tagtype"))
					{
						if (!tagtype.is_empty()) tagtype += "|";
						tagtype += field_value;
					}
				}
			}
		}
		if (!tagtype.is_empty()) info_set("tagtype",tagtype);
		set_replaygain(rg);
	}
}

void file_info::overwrite_info(const file_info & p_source) {
	t_size count = p_source.info_get_count();
	for(t_size n=0;n<count;n++) {
		info_set(p_source.info_enum_name(n),p_source.info_enum_value(n));
	}
}


void file_info::merge_fallback(const file_info & source) {
	set_replaygain( replaygain_info::g_merge(get_replaygain(), source.get_replaygain() ) );
	if (get_length() <= 0) set_length(source.get_length());
	t_size count = source.info_get_count();
	for(t_size infoWalk = 0; infoWalk < count; ++infoWalk) {
		const char * name = source.info_enum_name(infoWalk);
		if (!info_exists(name)) __info_add_unsafe(name, source.info_enum_value(infoWalk));
	}
	count = source.meta_get_count();
	for(t_size metaWalk = 0; metaWalk < count; ++metaWalk) {
		const char * name = source.meta_enum_name(metaWalk);
		if (!meta_exists(name)) _copy_meta_single_nocheck(source, metaWalk);
	}
}

static const char _tagtype[] = "tagtype";

static bool isSC( const char * n ) {
	return pfc::string_has_prefix_i(n, "Apple SoundCheck" );
}

void file_info::_set_tag(const file_info & tag) {
	this->copy_meta(tag);
	this->set_replaygain( replaygain_info::g_merge( this->get_replaygain(), tag.get_replaygain() ) );

	const size_t iCount = tag.info_get_count();
	for( size_t iWalk = 0; iWalk < iCount; ++iWalk ) {
		auto n = tag.info_enum_name(iWalk);
		if ( pfc::stringEqualsI_ascii( n, _tagtype ) || isSC(n) ) {
			this->info_set(n, tag.info_enum_value( iWalk ) );
		}
	}
    
#ifdef FOOBAR2000_FILE_INFO_PICTURES
    {
        auto p = tag.info_get("pictures");
        if ( p != nullptr ) this->info_set("pictures", p);
    }
#endif
}

void file_info::_add_tag(const file_info & otherTag) {
	this->set_replaygain( replaygain_info::g_merge( this->get_replaygain(), otherTag.get_replaygain() ) );

	const char * tt1 = this->info_get(_tagtype);
	const char * tt2 = otherTag.info_get(_tagtype);
	if (tt2) {
		if (tt1) {
			this->info_set(_tagtype, PFC_string_formatter() << tt1 << "|" << tt2);
		} else {
			this->info_set(_tagtype, tt2);
		}
	}

	{
		const size_t iCount = otherTag.info_get_count();
		for( size_t w = 0; w < iCount; ++ w ) {
			auto n = otherTag.info_enum_name(w);
			if (isSC(n) && !this->info_get(n)) {
				this->info_set( n, otherTag.info_enum_value(w) );
			}
		}
	}
    
#ifdef FOOBAR2000_FILE_INFO_PICTURES
    if (this->info_get("pictures") == nullptr) {
        auto p = otherTag.info_get("pictures");
        if ( p != nullptr ) info_set("pictures", p);
    }
#endif
}
