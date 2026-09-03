#include "Probe.hpp"

#include "ContentRange.hpp"
#include "CurlEasy.hpp"
#include "FilenameResolver.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace {
std::optional<std::uint64_t> parse_u64(const std::string& raw) {
    const std::string s = ResponseHeaders::trim(raw);
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front()))) {
        return std::nullopt;
    }
    try {
        std::size_t pos = 0;
        const unsigned long long v = std::stoull(s, &pos);
        if (pos != s.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(v);
    } catch (...) {
        return std::nullopt;
    }
}
void note(DownloadPlan& plan, const std::string& line) {
    plan.notes += "  " + line + "\n";
}

}   // namespace

Probe::Probe(ProbeContext ctx) : ctx_(std::move(ctx)) {}

DownloadPlan Probe::run() {
    if (ctx_.url.empty()) {
        throw std::invalid_argument("ProbeContext::url is empty");
    }
    DownloadPlan plan;
    plan.requested_url = ctx_.url;
    CurlEasy curl;
    bool conclusive = false;
    if (ctx_.use_range_get) {
        conclusive = probe_range_get(curl, plan);
    }
    if (!conclusive && ctx_.fallback_to_head) {
        conclusive = probe_head(curl, plan);
    }
    if (conclusive) {
        plan.mode = decide_mode(plan);
    } else {
        note(plan, "no conclusion, mode kept Unknown");
    }
    return plan;
}

std::size_t Probe::discard_body(char* ptr, std::size_t size,
                                std::size_t nmemb, void* userdata) {
    const std::size_t n = size * nmemb;
    auto* self = static_cast<Probe*>(userdata);
    if (!self) {
        return n;
    }
    self->body_bytes_seen_ += n;
    if (self->body_bytes_seen_ > kMaxProbeBody) {
        return 0;
    }
    return n;
}

void Probe::apply_common_options(CurlEasy& curl) {
    curl.setopt(CURLOPT_URL, ctx_.url.c_str());
    curl.setopt(CURLOPT_FOLLOWLOCATION, ctx_.follow_redirects ? 1L : 0L);
    curl.setopt(CURLOPT_MAXREDIRS, ctx_.max_redirects);
    curl.setopt(CURLOPT_CONNECTTIMEOUT, ctx_.connect_timeout_s);
    curl.setopt(CURLOPT_TIMEOUT, ctx_.total_timeout_s);
    curl.setopt(CURLOPT_USERAGENT, ctx_.user_agent.c_str());
    curl.setopt(CURLOPT_HEADERFUNCTION, &ResponseHeaders::curl_callback);
    curl.setopt(CURLOPT_HEADERDATA, &headers_);
    curl.setopt(CURLOPT_WRITEFUNCTION, &Probe::discard_body);
    curl.setopt(CURLOPT_WRITEDATA, this);
    curl.setopt(CURLOPT_NOSIGNAL, 1L);
    curl.setopt(CURLOPT_VERBOSE, ctx_.verbose ? 1L : 0L);
}

bool Probe::probe_range_get(CurlEasy& curl, DownloadPlan& plan) {
    curl.reset();
    headers_.clear();
    body_bytes_seen_ = 0;
    apply_common_options(curl);

    CurlSlist req_headers;
    req_headers.append("Range: bytes=0-0");
    for (const auto& h : ctx_.extra_headers) {
        req_headers.append(h);
    }
    curl.setopt(CURLOPT_HTTPHEADER, req_headers.get());
    curl.setopt(CURLOPT_HTTPGET, 1L);
    const CURLcode code = curl.perform();//发送网络请求
    if (code != CURLE_OK && code != CURLE_WRITE_ERROR) {
        note(plan, std::string("range-get failed: ") + curl_easy_strerror(code));
        return false;   // 没结论，让 HEAD 再试一次
    }
    plan.effective_url = curl.get_info_string(CURLINFO_EFFECTIVE_URL);
    plan.probe_status  = curl.get_info_long(CURLINFO_RESPONSE_CODE);
    plan.accept_ranges_header = headers_.get("accept-ranges");
    fill_common_fields(plan);
    const long status = plan.probe_status;

    if (status == 206) {
        const auto cr_raw = headers_.get("content-range");
        if (!cr_raw) {
            note(plan, "206 without Content-Range -- not trustworthy, Range support not confirmed");
            return false;
        }
        const auto cr = ContentRange::parse(*cr_raw);
        if (!cr || !cr->matches(0, 0)) {
            note(plan, "206 but Content-Range is not bytes 0-0/*: " + *cr_raw);
            return false;
        }
        plan.supports_range = true;
        if (cr->total) {
            plan.total_size = *cr->total;
            note(plan, "206 verified, total size from Content-Range: " +
                           std::to_string(*cr->total));
        } else {
            note(plan, "206 verified, but Content-Range total is * -- total size unknown");
        }
        return true;
    }
    if (status == 200) {
        plan.supports_range = false;
        if (const auto cl = headers_.get("content-length")) {
            plan.total_size = parse_u64(*cl);
        }
        if (plan.total_size) {
            note(plan, "200 -- server ignored Range, total size from Content-Length: " +
                           std::to_string(*plan.total_size));
        } else {
            note(plan, "200 without Content-Length (chunked) -- total size unknown");
        }

        if (plan.accept_ranges_header &&
            ResponseHeaders::to_lower(*plan.accept_ranges_header).find("bytes") !=
                std::string::npos) {
            note(plan, "Accept-Ranges claims bytes, but the probe got 200 -- trusting the probe");
        }
        return true;
    }
    if (status == 416) {
        if (const auto cr_raw = headers_.get("content-range")) {
            const auto slash = cr_raw->rfind('/');
            if (slash != std::string::npos) {
                plan.total_size = parse_u64(cr_raw->substr(slash + 1));
            }
        }
        plan.supports_range = plan.total_size.has_value();
        note(plan, "416 -- zero-length file, server does understand Range semantics");
        return plan.total_size.has_value();
    }

    if (status == 405 || status == 501) {
        note(plan, std::to_string(status) + " -- GET rejected, falling back to HEAD");
        return false;
    }

    note(plan, "unexpected status " + std::to_string(status) + " -- inconclusive");
    return false;
}

bool Probe::probe_head(CurlEasy& curl, DownloadPlan& plan) {
    curl.reset();
    headers_.clear();
    body_bytes_seen_ = 0;
    apply_common_options(curl);

    CurlSlist req_headers;
    for (const auto& h : ctx_.extra_headers) {
        req_headers.append(h);
    }
    if (req_headers.get()) {
        curl.setopt(CURLOPT_HTTPHEADER, req_headers.get());
    }
    curl.setopt(CURLOPT_NOBODY, 1L);   // = HEAD

    const CURLcode code = curl.perform();
    if (code != CURLE_OK) {
        note(plan, std::string("head failed: ") + curl_easy_strerror(code));
        return false;
    }

    plan.effective_url = curl.get_info_string(CURLINFO_EFFECTIVE_URL);
    plan.probe_status  = curl.get_info_long(CURLINFO_RESPONSE_CODE);
    if (!plan.accept_ranges_header) {
        plan.accept_ranges_header = headers_.get("accept-ranges");
    }
    fill_common_fields(plan);

    if (plan.probe_status < 200 || plan.probe_status >= 300) {
        note(plan, "head status " + std::to_string(plan.probe_status) + " -- inconclusive");
        return false;
    }

    if (const auto cl = headers_.get("content-length")) {
        plan.total_size = parse_u64(*cl);
    }
    plan.supports_range = false;
    note(plan, "head fallback: got total size, but Range support was never verified -- "
               "conservatively treated as unsupported");

    return plan.total_size.has_value();
}

void Probe::fill_common_fields( DownloadPlan& plan) const {
    plan.etag          = headers_.get("etag");
    plan.last_modified = headers_.get("last-modified");
    plan.content_type  = headers_.get("content-type");
    const std::string& url_for_name =
        plan.effective_url.empty() ? plan.requested_url : plan.effective_url;
    plan.suggested_filename =
        FilenameResolver::resolve(headers_.get("content-disposition"), url_for_name);
}

TransferMode Probe::decide_mode(const DownloadPlan& plan) noexcept {
    if (!plan.size_known()) {
        return TransferMode::StreamingChunked;
    }
    if (*plan.total_size == 0) {
        return TransferMode::SingleResumable;
    }
    return plan.supports_range ? TransferMode::RangedParallel
                               : TransferMode::SingleResumable;
}


