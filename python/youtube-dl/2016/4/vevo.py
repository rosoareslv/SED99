from __future__ import unicode_literals

import re

from .common import InfoExtractor
from ..compat import compat_etree_fromstring
from ..utils import (
    ExtractorError,
    int_or_none,
    sanitized_Request,
    parse_iso8601,
)


class VevoIE(InfoExtractor):
    '''
    Accepts urls from vevo.com or in the format 'vevo:{id}'
    (currently used by MTVIE and MySpaceIE)
    '''
    _VALID_URL = r'''(?x)
        (?:https?://www\.vevo\.com/watch/(?:[^/]+/(?:[^/]+/)?)?|
           https?://cache\.vevo\.com/m/html/embed\.html\?video=|
           https?://videoplayer\.vevo\.com/embed/embedded\?videoId=|
           vevo:)
        (?P<id>[^&?#]+)'''

    _TESTS = [{
        'url': 'http://www.vevo.com/watch/hurts/somebody-to-die-for/GB1101300280',
        'md5': '95ee28ee45e70130e3ab02b0f579ae23',
        'info_dict': {
            'id': 'GB1101300280',
            'ext': 'mp4',
            'title': 'Somebody to Die For',
            'upload_date': '20130624',
            'uploader': 'Hurts',
            'timestamp': 1372057200,
        },
    }, {
        'note': 'v3 SMIL format',
        'url': 'http://www.vevo.com/watch/cassadee-pope/i-wish-i-could-break-your-heart/USUV71302923',
        'md5': 'f6ab09b034f8c22969020b042e5ac7fc',
        'info_dict': {
            'id': 'USUV71302923',
            'ext': 'mp4',
            'title': 'I Wish I Could Break Your Heart',
            'upload_date': '20140219',
            'uploader': 'Cassadee Pope',
            'timestamp': 1392796919,
        },
    }, {
        'note': 'Age-limited video',
        'url': 'https://www.vevo.com/watch/justin-timberlake/tunnel-vision-explicit/USRV81300282',
        'info_dict': {
            'id': 'USRV81300282',
            'ext': 'mp4',
            'title': 'Tunnel Vision (Explicit)',
            'upload_date': '20130703',
            'age_limit': 18,
            'uploader': 'Justin Timberlake',
            'timestamp': 1372888800,
        },
    }, {
        'note': 'No video_info',
        'url': 'http://www.vevo.com/watch/k-camp-1/Till-I-Die/USUV71503000',
        'md5': '8b83cc492d72fc9cf74a02acee7dc1b0',
        'info_dict': {
            'id': 'USUV71503000',
            'ext': 'mp4',
            'title': 'Till I Die',
            'upload_date': '20151207',
            'age_limit': 18,
            'uploader': 'K Camp',
            'timestamp': 1449468000,
        },
    }]
    _SMIL_BASE_URL = 'http://smil.lvl3.vevo.com'
    _SOURCE_TYPES = {
        0: 'youtube',
        1: 'brightcove',
        2: 'http',
        3: 'hls_ios',
        4: 'hls',
        5: 'smil',  # http
        7: 'f4m_cc',
        8: 'f4m_ak',
        9: 'f4m_l3',
        10: 'ism',
        13: 'smil',  # rtmp
        18: 'dash',
    }
    _VERSIONS = {
        0: 'youtube',  # only in AuthenticateVideo videoVersions
        1: 'level3',
        2: 'akamai',
        3: 'level3',
        4: 'amazon',
    }

    def _parse_smil_formats(self, smil, smil_url, video_id, namespace=None, f4m_params=None, transform_rtmp_url=None):
        formats = []
        els = smil.findall('.//{http://www.w3.org/2001/SMIL20/Language}video')
        for el in els:
            src = el.attrib['src']
            m = re.match(r'''(?xi)
                (?P<ext>[a-z0-9]+):
                (?P<path>
                    [/a-z0-9]+     # The directory and main part of the URL
                    _(?P<tbr>[0-9]+)k
                    _(?P<width>[0-9]+)x(?P<height>[0-9]+)
                    _(?P<vcodec>[a-z0-9]+)
                    _(?P<vbr>[0-9]+)
                    _(?P<acodec>[a-z0-9]+)
                    _(?P<abr>[0-9]+)
                    \.[a-z0-9]+  # File extension
                )''', src)
            if not m:
                continue

            format_url = self._SMIL_BASE_URL + m.group('path')
            formats.append({
                'url': format_url,
                'format_id': 'smil_' + m.group('tbr'),
                'vcodec': m.group('vcodec'),
                'acodec': m.group('acodec'),
                'tbr': int(m.group('tbr')),
                'vbr': int(m.group('vbr')),
                'abr': int(m.group('abr')),
                'ext': m.group('ext'),
                'width': int(m.group('width')),
                'height': int(m.group('height')),
            })
        return formats

    def _initialize_api(self, video_id):
        req = sanitized_Request(
            'http://www.vevo.com/auth', data=b'')
        webpage = self._download_webpage(
            req, None,
            note='Retrieving oauth token',
            errnote='Unable to retrieve oauth token')

        if 'THIS PAGE IS CURRENTLY UNAVAILABLE IN YOUR REGION' in webpage:
            raise ExtractorError(
                '%s said: This page is currently unavailable in your region.' % self.IE_NAME, expected=True)

        auth_info = self._parse_json(webpage, video_id)
        self._api_url_template = self.http_scheme() + '//apiv2.vevo.com/%s?token=' + auth_info['access_token']

    def _call_api(self, path, video_id, note, errnote, fatal=True):
        return self._download_json(self._api_url_template % path, video_id, note, errnote)

    def _real_extract(self, url):
        video_id = self._match_id(url)

        json_url = 'http://api.vevo.com/VideoService/AuthenticateVideo?isrc=%s' % video_id
        response = self._download_json(
            json_url, video_id, 'Downloading video info', 'Unable to download info')
        video_info = response.get('video') or {}
        video_versions = video_info.get('videoVersions')
        uploader = None
        timestamp = None
        view_count = None
        formats = []

        if not video_info:
            if response.get('statusCode') != 909:
                ytid = response.get('errorInfo', {}).get('ytid')
                if ytid:
                    self.report_warning(
                        'Video is geoblocked, trying with the YouTube video %s' % ytid)
                    return self.url_result(ytid, 'Youtube', ytid)

                if 'statusMessage' in response:
                    raise ExtractorError('%s said: %s' % (
                        self.IE_NAME, response['statusMessage']), expected=True)
                raise ExtractorError('Unable to extract videos')

            self._initialize_api(video_id)
            video_info = self._call_api(
                'video/%s' % video_id, video_id, 'Downloading api video info',
                'Failed to download video info')

            video_versions = self._call_api(
                'video/%s/streams' % video_id, video_id,
                'Downloading video versions info',
                'Failed to download video versions info')

            timestamp = parse_iso8601(video_info.get('releaseDate'))
            artists = video_info.get('artists')
            if artists:
                uploader = artists[0]['name']
            view_count = int_or_none(video_info.get('views', {}).get('total'))

            for video_version in video_versions:
                version = self._VERSIONS.get(video_version['version'])
                version_url = video_version.get('url')
                if not version_url:
                    continue

                if '.ism' in version_url:
                    continue
                elif '.mpd' in version_url:
                    formats.extend(self._extract_mpd_formats(
                        version_url, video_id, mpd_id='dash-%s' % version,
                        note='Downloading %s MPD information' % version,
                        errnote='Failed to download %s MPD information' % version,
                        fatal=False))
                elif '.m3u8' in version_url:
                    formats.extend(self._extract_m3u8_formats(
                        version_url, video_id, 'mp4', 'm3u8_native',
                        m3u8_id='hls-%s' % version,
                        note='Downloading %s m3u8 information' % version,
                        errnote='Failed to download %s m3u8 information' % version,
                        fatal=False))
                else:
                    m = re.search(r'''(?xi)
                        _(?P<width>[0-9]+)x(?P<height>[0-9]+)
                        _(?P<vcodec>[a-z0-9]+)
                        _(?P<vbr>[0-9]+)
                        _(?P<acodec>[a-z0-9]+)
                        _(?P<abr>[0-9]+)
                        \.(?P<ext>[a-z0-9]+)''', version_url)
                    if not m:
                        continue

                    formats.append({
                        'url': version_url,
                        'format_id': 'http-%s-%s' % (version, video_version['quality']),
                        'vcodec': m.group('vcodec'),
                        'acodec': m.group('acodec'),
                        'vbr': int(m.group('vbr')),
                        'abr': int(m.group('abr')),
                        'ext': m.group('ext'),
                        'width': int(m.group('width')),
                        'height': int(m.group('height')),
                    })
        else:
            timestamp = int_or_none(self._search_regex(
                r'/Date\((\d+)\)/',
                video_info['releaseDate'], 'release date', fatal=False),
                scale=1000)
            artists = video_info.get('mainArtists')
            if artists:
                uploader = artists[0]['artistName']

            smil_parsed = False
            for video_version in video_info['videoVersions']:
                version = self._VERSIONS.get(video_version['version'])
                if version == 'youtube':
                    continue
                else:
                    source_type = self._SOURCE_TYPES.get(video_version['sourceType'])
                    renditions = compat_etree_fromstring(video_version['data'])
                    if source_type == 'http':
                        for rend in renditions.findall('rendition'):
                            attr = rend.attrib
                            formats.append({
                                'url': attr['url'],
                                'format_id': 'http-%s-%s' % (version, attr['name']),
                                'height': int_or_none(attr.get('frameheight')),
                                'width': int_or_none(attr.get('frameWidth')),
                                'tbr': int_or_none(attr.get('totalBitrate')),
                                'vbr': int_or_none(attr.get('videoBitrate')),
                                'abr': int_or_none(attr.get('audioBitrate')),
                                'vcodec': attr.get('videoCodec'),
                                'acodec': attr.get('audioCodec'),
                            })
                    elif source_type == 'hls':
                        formats.extend(self._extract_m3u8_formats(
                            renditions.find('rendition').attrib['url'], video_id,
                            'mp4', 'm3u8_native', m3u8_id='hls-%s' % version,
                            note='Downloading %s m3u8 information' % version,
                            errnote='Failed to download %s m3u8 information' % version,
                            fatal=False))
                    elif source_type == 'smil' and version == 'level3' and not smil_parsed:
                        formats.extend(self._extract_smil_formats(
                            renditions.find('rendition').attrib['url'], video_id, False))
                        smil_parsed = True
        self._sort_formats(formats)

        title = video_info['title']

        is_explicit = video_info.get('isExplicit')
        if is_explicit is True:
            age_limit = 18
        elif is_explicit is False:
            age_limit = 0
        else:
            age_limit = None

        duration = video_info.get('duration')

        return {
            'id': video_id,
            'title': title,
            'formats': formats,
            'thumbnail': video_info.get('imageUrl') or video_info.get('thumbnailUrl'),
            'timestamp': timestamp,
            'uploader': uploader,
            'duration': duration,
            'view_count': view_count,
            'age_limit': age_limit,
        }
