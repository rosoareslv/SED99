# encoding: utf-8
from __future__ import unicode_literals

import re

from .common import InfoExtractor
from ..compat import (
    compat_parse_qs,
    compat_urllib_parse_urlparse,
)
from ..utils import (
    find_xpath_attr,
    unified_strdate,
    get_element_by_attribute,
    int_or_none,
    NO_DEFAULT,
    qualities,
)

# There are different sources of video in arte.tv, the extraction process
# is different for each one. The videos usually expire in 7 days, so we can't
# add tests.


class ArteTvIE(InfoExtractor):
    _VALID_URL = r'https?://videos\.arte\.tv/(?P<lang>fr|de|en|es)/.*-(?P<id>.*?)\.html'
    IE_NAME = 'arte.tv'

    def _real_extract(self, url):
        mobj = re.match(self._VALID_URL, url)
        lang = mobj.group('lang')
        video_id = mobj.group('id')

        ref_xml_url = url.replace('/videos/', '/do_delegate/videos/')
        ref_xml_url = ref_xml_url.replace('.html', ',view,asPlayerXml.xml')
        ref_xml_doc = self._download_xml(
            ref_xml_url, video_id, note='Downloading metadata')
        config_node = find_xpath_attr(ref_xml_doc, './/video', 'lang', lang)
        config_xml_url = config_node.attrib['ref']
        config = self._download_xml(
            config_xml_url, video_id, note='Downloading configuration')

        formats = [{
            'format_id': q.attrib['quality'],
            # The playpath starts at 'mp4:', if we don't manually
            # split the url, rtmpdump will incorrectly parse them
            'url': q.text.split('mp4:', 1)[0],
            'play_path': 'mp4:' + q.text.split('mp4:', 1)[1],
            'ext': 'flv',
            'quality': 2 if q.attrib['quality'] == 'hd' else 1,
        } for q in config.findall('./urls/url')]
        self._sort_formats(formats)

        title = config.find('.//name').text
        thumbnail = config.find('.//firstThumbnailUrl').text
        return {
            'id': video_id,
            'title': title,
            'thumbnail': thumbnail,
            'formats': formats,
        }


class ArteTVPlus7IE(InfoExtractor):
    IE_NAME = 'arte.tv:+7'
    _VALID_URL = r'https?://(?:www\.)?arte\.tv/guide/(?P<lang>fr|de|en|es)/(?:(?:sendungen|emissions|embed)/)?(?P<id>[^/]+)/(?P<name>[^/?#&]+)'

    @classmethod
    def _extract_url_info(cls, url):
        mobj = re.match(cls._VALID_URL, url)
        lang = mobj.group('lang')
        query = compat_parse_qs(compat_urllib_parse_urlparse(url).query)
        if 'vid' in query:
            video_id = query['vid'][0]
        else:
            # This is not a real id, it can be for example AJT for the news
            # http://www.arte.tv/guide/fr/emissions/AJT/arte-journal
            video_id = mobj.group('id')
        return video_id, lang

    def _real_extract(self, url):
        video_id, lang = self._extract_url_info(url)
        webpage = self._download_webpage(url, video_id)
        return self._extract_from_webpage(webpage, video_id, lang)

    def _extract_from_webpage(self, webpage, video_id, lang):
        patterns_templates = (r'arte_vp_url=["\'](.*?%s.*?)["\']', r'data-url=["\']([^"]+%s[^"]+)["\']')
        ids = (video_id, '')
        # some pages contain multiple videos (like
        # http://www.arte.tv/guide/de/sendungen/XEN/xenius/?vid=055918-015_PLUS7-D),
        # so we first try to look for json URLs that contain the video id from
        # the 'vid' parameter.
        patterns = [t % re.escape(_id) for _id in ids for t in patterns_templates]
        json_url = self._html_search_regex(
            patterns, webpage, 'json vp url', default=None)
        if not json_url:
            def find_iframe_url(webpage, default=NO_DEFAULT):
                return self._html_search_regex(
                    r'<iframe[^>]+src=(["\'])(?P<url>.+\bjson_url=.+?)\1',
                    webpage, 'iframe url', group='url', default=default)

            iframe_url = find_iframe_url(webpage, None)
            if not iframe_url:
                embed_url = self._html_search_regex(
                    r'arte_vp_url_oembed=\'([^\']+?)\'', webpage, 'embed url', default=None)
                if embed_url:
                    player = self._download_json(
                        embed_url, video_id, 'Downloading player page')
                    iframe_url = find_iframe_url(player['html'])
            # en and es URLs produce react-based pages with different layout (e.g.
            # http://www.arte.tv/guide/en/053330-002-A/carnival-italy?zone=world)
            if not iframe_url:
                program = self._search_regex(
                    r'program\s*:\s*({.+?["\']embed_html["\'].+?}),?\s*\n',
                    webpage, 'program', default=None)
                if program:
                    embed_html = self._parse_json(program, video_id)
                    if embed_html:
                        iframe_url = find_iframe_url(embed_html['embed_html'])
            if iframe_url:
                json_url = compat_parse_qs(
                    compat_urllib_parse_urlparse(iframe_url).query)['json_url'][0]
        if json_url:
            title = self._search_regex(
                r'<h3[^>]+title=(["\'])(?P<title>.+?)\1',
                webpage, 'title', default=None, group='title')
            return self._extract_from_json_url(json_url, video_id, lang, title=title)
        # Different kind of embed URL (e.g.
        # http://www.arte.tv/magazine/trepalium/fr/episode-0406-replay-trepalium)
        embed_url = self._search_regex(
            r'<iframe[^>]+src=(["\'])(?P<url>.+?)\1',
            webpage, 'embed url', group='url')
        return self.url_result(embed_url)

    def _extract_from_json_url(self, json_url, video_id, lang, title=None):
        info = self._download_json(json_url, video_id)
        player_info = info['videoJsonPlayer']

        upload_date_str = player_info.get('shootingDate')
        if not upload_date_str:
            upload_date_str = (player_info.get('VRA') or player_info.get('VDA') or '').split(' ')[0]

        title = (player_info.get('VTI') or title or player_info['VID']).strip()
        subtitle = player_info.get('VSU', '').strip()
        if subtitle:
            title += ' - %s' % subtitle

        info_dict = {
            'id': player_info['VID'],
            'title': title,
            'description': player_info.get('VDE'),
            'upload_date': unified_strdate(upload_date_str),
            'thumbnail': player_info.get('programImage') or player_info.get('VTU', {}).get('IUR'),
        }
        qfunc = qualities(['HQ', 'MQ', 'EQ', 'SQ'])

        LANGS = {
            'fr': 'F',
            'de': 'A',
            'en': 'E[ANG]',
            'es': 'E[ESP]',
        }

        formats = []
        for format_id, format_dict in player_info['VSR'].items():
            f = dict(format_dict)
            versionCode = f.get('versionCode')
            langcode = LANGS.get(lang, lang)
            lang_rexs = [r'VO?%s-' % re.escape(langcode), r'VO?.-ST%s$' % re.escape(langcode)]
            lang_pref = None
            if versionCode:
                matched_lang_rexs = [r for r in lang_rexs if re.match(r, versionCode)]
                lang_pref = -10 if not matched_lang_rexs else 10 * len(matched_lang_rexs)
            source_pref = 0
            if versionCode is not None:
                # The original version with subtitles has lower relevance
                if re.match(r'VO-ST(F|A|E)', versionCode):
                    source_pref -= 10
                # The version with sourds/mal subtitles has also lower relevance
                elif re.match(r'VO?(F|A|E)-STM\1', versionCode):
                    source_pref -= 9
            format = {
                'format_id': format_id,
                'preference': -10 if f.get('videoFormat') == 'M3U8' else None,
                'language_preference': lang_pref,
                'format_note': '%s, %s' % (f.get('versionCode'), f.get('versionLibelle')),
                'width': int_or_none(f.get('width')),
                'height': int_or_none(f.get('height')),
                'tbr': int_or_none(f.get('bitrate')),
                'quality': qfunc(f.get('quality')),
                'source_preference': source_pref,
            }

            if f.get('mediaType') == 'rtmp':
                format['url'] = f['streamer']
                format['play_path'] = 'mp4:' + f['url']
                format['ext'] = 'flv'
            else:
                format['url'] = f['url']

            formats.append(format)

        self._check_formats(formats, video_id)
        self._sort_formats(formats)

        info_dict['formats'] = formats
        return info_dict


# It also uses the arte_vp_url url from the webpage to extract the information
class ArteTVCreativeIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:creative'
    _VALID_URL = r'https?://creative\.arte\.tv/(?P<lang>fr|de|en|es)/(?:[^/]+/)*(?P<id>[^/?#&]+)'

    _TESTS = [{
        'url': 'http://creative.arte.tv/de/magazin/agentur-amateur-corporate-design',
        'info_dict': {
            'id': '72176',
            'ext': 'mp4',
            'title': 'Folge 2 - Corporate Design',
            'upload_date': '20131004',
        },
    }, {
        'url': 'http://creative.arte.tv/fr/Monty-Python-Reunion',
        'info_dict': {
            'id': '160676',
            'ext': 'mp4',
            'title': 'Monty Python live (mostly)',
            'description': 'Événement ! Quarante-cinq ans après leurs premiers succès, les légendaires Monty Python remontent sur scène.\n',
            'upload_date': '20140805',
        }
    }, {
        'url': 'http://creative.arte.tv/de/episode/agentur-amateur-4-der-erste-kunde',
        'only_matching': True,
    }]


class ArteTVInfoIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:info'
    _VALID_URL = r'https?://info\.arte\.tv/(?P<lang>fr|de|en|es)/(?:[^/]+/)*(?P<id>[^/?#&]+)'

    _TEST = {
        'url': 'http://info.arte.tv/fr/service-civique-un-cache-misere',
        'info_dict': {
            'id': '067528-000-A',
            'ext': 'mp4',
            'title': 'Service civique, un cache misère ?',
            'upload_date': '20160403',
        },
    }


class ArteTVFutureIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:future'
    _VALID_URL = r'https?://future\.arte\.tv/(?P<lang>fr|de|en|es)/(?P<id>[^/?#&]+)'

    _TESTS = [{
        'url': 'http://future.arte.tv/fr/info-sciences/les-ecrevisses-aussi-sont-anxieuses',
        'info_dict': {
            'id': '050940-028-A',
            'ext': 'mp4',
            'title': 'Les écrevisses aussi peuvent être anxieuses',
            'upload_date': '20140902',
        },
    }, {
        'url': 'http://future.arte.tv/fr/la-science-est-elle-responsable',
        'only_matching': True,
    }]


class ArteTVDDCIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:ddc'
    _VALID_URL = r'https?://ddc\.arte\.tv/(?P<lang>emission|folge)/(?P<id>[^/?#&]+)'

    def _real_extract(self, url):
        video_id, lang = self._extract_url_info(url)
        if lang == 'folge':
            lang = 'de'
        elif lang == 'emission':
            lang = 'fr'
        webpage = self._download_webpage(url, video_id)
        scriptElement = get_element_by_attribute('class', 'visu_video_block', webpage)
        script_url = self._html_search_regex(r'src="(.*?)"', scriptElement, 'script url')
        javascriptPlayerGenerator = self._download_webpage(script_url, video_id, 'Download javascript player generator')
        json_url = self._search_regex(r"json_url=(.*)&rendering_place.*", javascriptPlayerGenerator, 'json url')
        return self._extract_from_json_url(json_url, video_id, lang)


class ArteTVConcertIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:concert'
    _VALID_URL = r'https?://concert\.arte\.tv/(?P<lang>fr|de|en|es)/(?P<id>[^/?#&]+)'

    _TEST = {
        'url': 'http://concert.arte.tv/de/notwist-im-pariser-konzertclub-divan-du-monde',
        'md5': '9ea035b7bd69696b67aa2ccaaa218161',
        'info_dict': {
            'id': '186',
            'ext': 'mp4',
            'title': 'The Notwist im Pariser Konzertclub "Divan du Monde"',
            'upload_date': '20140128',
            'description': 'md5:486eb08f991552ade77439fe6d82c305',
        },
    }


class ArteTVCinemaIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:cinema'
    _VALID_URL = r'https?://cinema\.arte\.tv/(?P<lang>fr|de|en|es)/(?P<id>.+)'

    _TEST = {
        'url': 'http://cinema.arte.tv/de/node/38291',
        'md5': '6b275511a5107c60bacbeeda368c3aa1',
        'info_dict': {
            'id': '055876-000_PWA12025-D',
            'ext': 'mp4',
            'title': 'Tod auf dem Nil',
            'upload_date': '20160122',
            'description': 'md5:7f749bbb77d800ef2be11d54529b96bc',
        },
    }


class ArteTVMagazineIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:magazine'
    _VALID_URL = r'https?://(?:www\.)?arte\.tv/magazine/[^/]+/(?P<lang>fr|de|en|es)/(?P<id>[^/?#&]+)'

    _TESTS = [{
        # Embedded via <iframe src="http://www.arte.tv/arte_vp/index.php?json_url=..."
        'url': 'http://www.arte.tv/magazine/trepalium/fr/entretien-avec-le-realisateur-vincent-lannoo-trepalium',
        'md5': '2a9369bcccf847d1c741e51416299f25',
        'info_dict': {
            'id': '065965-000-A',
            'ext': 'mp4',
            'title': 'Trepalium - Extrait Ep.01',
            'upload_date': '20160121',
        },
    }, {
        # Embedded via <iframe src="http://www.arte.tv/guide/fr/embed/054813-004-A/medium"
        'url': 'http://www.arte.tv/magazine/trepalium/fr/episode-0406-replay-trepalium',
        'md5': 'fedc64fc7a946110fe311634e79782ca',
        'info_dict': {
            'id': '054813-004_PLUS7-F',
            'ext': 'mp4',
            'title': 'Trepalium (4/6)',
            'description': 'md5:10057003c34d54e95350be4f9b05cb40',
            'upload_date': '20160218',
        },
    }, {
        'url': 'http://www.arte.tv/magazine/metropolis/de/frank-woeste-german-paris-metropolis',
        'only_matching': True,
    }]


class ArteTVEmbedIE(ArteTVPlus7IE):
    IE_NAME = 'arte.tv:embed'
    _VALID_URL = r'''(?x)
        http://www\.arte\.tv
        /(?:playerv2/embed|arte_vp/index)\.php\?json_url=
        (?P<json_url>
            http://arte\.tv/papi/tvguide/videos/stream/player/
            (?P<lang>[^/]+)/(?P<id>[^/]+)[^&]*
        )
    '''

    def _real_extract(self, url):
        mobj = re.match(self._VALID_URL, url)
        video_id = mobj.group('id')
        lang = mobj.group('lang')
        json_url = mobj.group('json_url')
        return self._extract_from_json_url(json_url, video_id, lang)
