alter table public.user_reviews add column review_type varchar default 'server'
alter table public.user_reviews add column title varchar default ''
CREATE INDEX I_USER_REVIEWS_BY_TYPE ON public.user_reviews (review_type);
