#include <stdexcept>
#include <iostream>
#include "../include/db.h"

namespace {

    void exec_write(sqlite3* db, const std::string& sql, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] prepare failed: " << sqlite3_errmsg(db) << " SQL: " << sql << std::endl;
            throw std::runtime_error("exec_write prepare failed: " + std::string(sqlite3_errmsg(db)));
        }

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);  // clean up before throwing
            throw std::runtime_error("exec_write failed: " + std::string(sqlite3_errmsg(db)));
        }

        sqlite3_finalize(stmt);
    }

    bool column_exists(sqlite3* db, const std::string& table, const std::string& col) {
        sqlite3_stmt* stmt;
        std::string sql = "PRAGMA table_info(" + table + ")";
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (name && col == std::string(name)) {
                sqlite3_finalize(stmt);
                return true;
            }
        }
        sqlite3_finalize(stmt);
        return false;
    }

    void exec_query(sqlite3* db, const std::string& sql, const std::function<void(sqlite3_stmt*)> &callback, const std::vector<std::string>& params = {}) {
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] prepare failed: " << sqlite3_errmsg(db) << " SQL: " << sql << std::endl;
            throw std::runtime_error("exec_query prepare failed: " + std::string(sqlite3_errmsg(db)));
        }

        for (int i = 0; i < static_cast<int>(params.size()); i++) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            callback(stmt);
        }
        sqlite3_finalize(stmt);
    }

}

void delete_job(sqlite3* db, const std::string &job_id) {
    const std::string sql_delete_str = "DELETE FROM jobs WHERE job_id = ?";
    exec_write(db, sql_delete_str, {job_id});
    std::cout << "[DB] delete_job(" << job_id << "): " << sqlite3_changes(db) << " rows" << std::endl;
}

void update_job_field(sqlite3 *db, const std::string &job_id, const std::string& field, const std::string &value) {
    // Whitelist of allowed fields to prevent SQL injection
    static const std::vector<std::string> allowedFields = {
        "user_status", "rating", "notes", "availability_status", "application_url"
    };
    
    if (std::find(allowedFields.begin(), allowedFields.end(), field) == allowedFields.end()) {
        throw std::runtime_error("Invalid field name: " + field);
    }
    
    const std::string sql_update_str = "UPDATE jobs SET " + field + " = ? WHERE job_id = ?";
    exec_write(db, sql_update_str, {value, job_id});
}

void insert_or_update_job(sqlite3 *db, const Job &job) {
    const std::string sql = R"(
        INSERT INTO jobs (
            job_id, title, company_name, place, zipcode, canton_code,
            employment_grade, application_url, detail_url,
            initial_publication_date, publication_end_date, template_text,
            scraped_at, user_status, availability_status
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'),'unseen','active')
        ON CONFLICT(job_id) DO UPDATE SET
            title = excluded.title,
            company_name = CASE WHEN excluded.company_name != '' THEN excluded.company_name ELSE company_name END,
            scraped_at = excluded.scraped_at,
            availability_status = 'active'
    )";
    
    exec_write(db, sql, {
        job.job_id, job.title, job.company_name, job.place, job.zipcode,
        job.canton_code, std::to_string(job.employment_grade),
        job.application_url, job.detail_url, job.pub_date, job.end_date, job.template_text
    });
}

int bulk_soft_delete_by_fit_label(sqlite3* db, const std::string& fit_label) {
    exec_write(db, "UPDATE jobs SET user_status = 'deleted' WHERE LOWER(fit_label) = LOWER(?)", {fit_label});
    return sqlite3_changes(db);
}

int bulk_hard_delete_by_fit_label(sqlite3* db, const std::string& fit_label) {
    exec_write(db, "DELETE FROM jobs WHERE LOWER(fit_label) = LOWER(?)", {fit_label});
    return sqlite3_changes(db);
}

int restore_all_deleted(sqlite3* db) {
    exec_write(db, "UPDATE jobs SET user_status = 'unseen' WHERE user_status = 'deleted'", {});
    return sqlite3_changes(db);
}

int bulk_soft_delete_by_status(sqlite3* db, const std::string& status, int older_than_days) {
    if (older_than_days > 0) {
        const std::string sql = "UPDATE jobs SET user_status = 'deleted' WHERE user_status = ? AND scraped_at < date('now', '-' || ? || ' days')";
        exec_write(db, sql, {status, std::to_string(older_than_days)});
    } else {
        exec_write(db, "UPDATE jobs SET user_status = 'deleted' WHERE user_status = ?", {status});
    }
    return sqlite3_changes(db);
}

void delete_expired_jobs(sqlite3* db) {
    exec_write(db, R"(
        DELETE FROM jobs
        WHERE publication_end_date != '' AND publication_end_date < date('now')
    )", {});
}

void db_init(sqlite3 *db) {
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, R"(
        CREATE TABLE IF NOT EXISTS jobs (
            job_id                   TEXT PRIMARY KEY,
            title                    TEXT,
            company_name             TEXT,
            place                    TEXT,
            zipcode                  TEXT,
            canton_code              TEXT,
            employment_grade         INTEGER,
            application_url          TEXT,
            detail_url               TEXT,
            initial_publication_date TEXT,
            publication_end_date     TEXT,
            template_text            TEXT,
            scraped_at               TEXT,
            user_status              TEXT,
            rating                   INTEGER,
            notes                    TEXT,
            availability_status      TEXT
        );
    )", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("create db failed: " + msg);
    }
    sqlite3_free(errMsg);
}

void update_job_details(sqlite3* db, const Job& job) {
    const std::string sql = 
        "UPDATE jobs SET title = ?, company_name = CASE WHEN ? != '' THEN ? ELSE company_name END, "
        "place = ?, zipcode = ?, canton_code = ?, detail_url = ?, "
        "initial_publication_date = ?, publication_end_date = ?, template_text = ?, "
        "scraped_at = datetime('now') WHERE job_id = ?";
    
    exec_write(db, sql, {
        job.title, job.company_name, job.company_name,
        job.place, job.zipcode, job.canton_code,
        job.detail_url, job.pub_date, job.end_date,
        job.template_text, job.job_id
    });
}

std::vector<Job> get_jobs_needing_details(sqlite3* db) {
    std::vector<Job> jobs;
    const std::string sql = 
        "SELECT job_id, title, company_name, place, zipcode, canton_code, "
        "employment_grade, application_url, detail_url, initial_publication_date, "
        "publication_end_date, template_text "
        "FROM jobs "
        "WHERE template_text IS NULL OR template_text = '' "
        "ORDER BY initial_publication_date DESC "
        "LIMIT 100";
    
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        Job job;
        job.job_id = getColumn(stmt, 0);
        job.title = getColumn(stmt, 1);
        job.company_name = getColumn(stmt, 2);
        job.place = getColumn(stmt, 3);
        job.zipcode = getColumn(stmt, 4);
        job.canton_code = getColumn(stmt, 5);
        job.employment_grade = sqlite3_column_int(stmt, 6);
        job.application_url = getColumn(stmt, 7);
        job.detail_url = getColumn(stmt, 8);
        job.pub_date = getColumn(stmt, 9);
        job.end_date = getColumn(stmt, 10);
        job.template_text = getColumn(stmt, 11);
        jobs.push_back(job);
    }, {});
    
    return jobs;
}

std::string getColumn(sqlite3_stmt* s, int i) {
    const char* v = (const char*)sqlite3_column_text(s, i);
    return v ? v : "";
}

std::vector<JobRecord> get_all_jobs(sqlite3* db) {
    std::vector<JobRecord> jobs;
    const std::string sql = R"(
        SELECT job_id, title, company_name, place, zipcode, canton_code,
               employment_grade, application_url,
               user_status, rating, notes, availability_status, detail_url,
               initial_publication_date, publication_end_date, fit_score, fit_label,
               fit_summary, fit_reasoning, fit_checked_at, fit_profile_hash,
               template_text
        FROM jobs
    )";
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        JobRecord job;
        job.job_id              = getColumn(stmt, 0);
        job.title               = getColumn(stmt, 1);
        job.company_name        = getColumn(stmt, 2);
        job.place               = getColumn(stmt, 3);
        job.zipcode             = getColumn(stmt, 4);
        job.canton_code         = getColumn(stmt, 5);
        job.employment_grade    = sqlite3_column_int(stmt, 6);
        job.application_url     = getColumn(stmt, 7);
        job.user_status         = getColumn(stmt, 8);
        job.rating              = sqlite3_column_int(stmt, 9);
        job.notes               = getColumn(stmt, 10);
        job.availability_status = getColumn(stmt, 11);
        job.detail_url          = getColumn(stmt, 12);
        job.pub_date            = getColumn(stmt, 13);
        job.end_date            = getColumn(stmt, 14);
        job.fit_score           = sqlite3_column_int(stmt, 15);
        job.fit_label           = getColumn(stmt, 16);
        job.fit_summary         = getColumn(stmt, 17);
        job.fit_reasoning       = getColumn(stmt, 18);
        job.fit_checked_at      = getColumn(stmt, 19);
        job.fit_profile_hash    = getColumn(stmt, 20);
        job.template_text       = getColumn(stmt, 21);
        jobs.push_back(job);
    });
    return jobs;
}

void db_v2_init(sqlite3* db) {
    db_v2_ensure_tables(db);
}

void db_v2_ensure_tables(sqlite3* db) {
    if (!column_exists(db, "jobs", "fit_score"))      exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_score INTEGER;");
    if (!column_exists(db, "jobs", "fit_label"))      exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_label TEXT;");
    if (!column_exists(db, "jobs", "fit_summary"))   exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_summary TEXT;");
    if (!column_exists(db, "jobs", "fit_reasoning")) exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_reasoning TEXT;");
    if (!column_exists(db, "jobs", "fit_checked_at")) exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_checked_at TEXT;");
    if (!column_exists(db, "jobs", "fit_profile_hash")) exec_write(db, "ALTER TABLE jobs ADD COLUMN fit_profile_hash TEXT;");
}

std::optional<std::string> get_job_template_text(sqlite3* db, const std::string& job_id) {
    std::optional<std::string> result;
    exec_query(db, "SELECT template_text FROM jobs WHERE job_id = ?",
        [&](sqlite3_stmt* stmt) { result = getColumn(stmt, 0); },
        {job_id});
    return result;
}

void save_fit_result_v2(sqlite3* db, const std::string& job_id, int score,
                        const std::string& label, const std::string& summary,
                        const std::string& reasoning, const std::string& profile_hash) {
    exec_write(db, R"(
        UPDATE jobs SET fit_score=?, fit_label=?, fit_summary=?, fit_reasoning=?, fit_checked_at=datetime('now'), fit_profile_hash=?
        WHERE job_id=?
    )", {std::to_string(score), label, summary, reasoning, profile_hash, job_id});
}

void clear_fit_data(sqlite3* db, const std::string& job_id) {
    exec_write(db, R"(
        UPDATE jobs SET fit_score=0, fit_label=NULL, fit_summary=NULL,
        fit_reasoning=NULL, fit_checked_at=NULL, fit_profile_hash=NULL
        WHERE job_id=?
    )", {job_id});
    std::cout << "[DB] clear_fit_data(" << job_id << "): " << sqlite3_changes(db) << " rows" << std::endl;
}

void clear_all_fit_data(sqlite3* db) {
    exec_write(db, R"(
        UPDATE jobs SET fit_score=0, fit_label=NULL, fit_summary=NULL,
        fit_reasoning=NULL, fit_checked_at=NULL, fit_profile_hash=NULL
        WHERE fit_label IS NOT NULL
    )");
    std::cout << "[DB] clear_all_fit_data: " << sqlite3_changes(db) << " rows" << std::endl;
}

std::vector<JobRecord> get_jobs_needing_fitcheck_v2(sqlite3* db, int limit) {
    std::vector<JobRecord> jobs;
    const std::string sql = R"(
        SELECT job_id, title, company_name, place, zipcode, canton_code,
               employment_grade, application_url, fit_score, fit_label,
               fit_summary, fit_reasoning, fit_checked_at, fit_profile_hash,
               user_status, rating, notes, availability_status, detail_url,
               initial_publication_date, publication_end_date, template_text
        FROM jobs
        WHERE fit_label IS NULL AND template_text IS NOT NULL AND (user_status IS NULL OR user_status != 'deleted')
        ORDER BY initial_publication_date DESC
        LIMIT ?
    )";
    exec_query(db, sql, [&](sqlite3_stmt* stmt) {
        JobRecord job;
        job.job_id              = getColumn(stmt, 0);
        job.title               = getColumn(stmt, 1);
        job.company_name        = getColumn(stmt, 2);
        job.place               = getColumn(stmt, 3);
        job.zipcode             = getColumn(stmt, 4);
        job.canton_code         = getColumn(stmt, 5);
        job.employment_grade    = sqlite3_column_int(stmt, 6);
        job.application_url     = getColumn(stmt, 7);
        job.fit_score           = sqlite3_column_int(stmt, 8);
        job.fit_label           = getColumn(stmt, 9);
        job.fit_summary         = getColumn(stmt, 10);
        job.fit_reasoning       = getColumn(stmt, 11);
        job.fit_checked_at      = getColumn(stmt, 12);
        job.fit_profile_hash    = getColumn(stmt, 13);
        job.user_status         = getColumn(stmt, 14);
        job.rating              = sqlite3_column_int(stmt, 15);
        job.notes               = getColumn(stmt, 16);
        job.availability_status = getColumn(stmt, 17);
        job.detail_url          = getColumn(stmt, 18);
        job.pub_date            = getColumn(stmt, 19);
        job.end_date            = getColumn(stmt, 20);
        job.template_text       = getColumn(stmt, 21);
        jobs.push_back(job);
    }, {std::to_string(limit)});
    return jobs;
}