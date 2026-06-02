//
// Created by shops on 12/03/2026.
//

#ifndef JOB_APP_DB_H
#define JOB_APP_DB_H
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sqlite3.h"

struct Job {
    std::string job_id;
    std::string title;
    std::string company_name;
    std::string place;
    std::string zipcode;
    std::string canton_code;
    int         employment_grade {};
    std::string application_url;
    std::string detail_url;
    std::string pub_date;
    std::string end_date;
    std::string template_text;
    std::string source;  // 'jobs_ch' | 'linkedin'
};

struct JobRecord : Job {
    // User state
    std::string user_status;
    int         rating {};
    std::string notes;
    std::string availability_status;

    // V2 fit-check fields
    int         fit_score {};
    std::string fit_label;
    std::string fit_summary;
    std::string fit_reasoning;
    std::string fit_checked_at;
    std::string fit_profile_hash;
};

// Database initialization
void db_init(sqlite3* db);
void db_v2_init(sqlite3* db);
void db_v2_ensure_tables(sqlite3* db);

// Job CRUD
void insert_or_update_job(sqlite3* db, const Job& job);
void delete_job(sqlite3* db, const std::string& job_id);
void delete_expired_jobs(sqlite3* db);
int bulk_soft_delete_by_status(sqlite3* db, const std::string& status, int older_than_days = 0);
int bulk_soft_delete_by_fit_label(sqlite3* db, const std::string& fit_label);
int bulk_hard_delete_by_fit_label(sqlite3* db, const std::string& fit_label);
int restore_all_deleted(sqlite3* db);

// Job queries
std::vector<JobRecord> get_all_jobs(sqlite3* db);
std::vector<Job> get_jobs_needing_details(sqlite3* db);
std::vector<JobRecord> get_jobs_needing_fitcheck_v2(sqlite3* db, int limit);
std::optional<std::string> get_job_template_text(sqlite3* db, const std::string& job_id);

// Job updates
void update_job_details(sqlite3* db, const Job& job);
void update_job_field(sqlite3* db, const std::string& job_id, const std::string& field, const std::string& value);

// V2 fit-check writes
void save_fit_result_v2(sqlite3* db, const std::string& job_id, int score,
                        const std::string& label, const std::string& summary,
                        const std::string& reasoning, const std::string& profile_hash);

// Admin operations
void clear_fit_data(sqlite3* db, const std::string& job_id);
void clear_all_fit_data(sqlite3* db);

// DB helper
std::string getColumn(sqlite3_stmt* s, int i);

#endif //JOB_APP_DB_H
